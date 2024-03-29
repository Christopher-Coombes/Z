#include "assembler.h"

#include <ios>
#include <unordered_map>

using vm::assembler::AssemblerException;
using std::cout;

#define ASM_DEBUG(thing) if (isDebug) stream << IO_DEBUG << thing << IO_NORM "\n"
#define ASM_WRITERAW(thing, sz) outputFile.write(thing, sz); byteCounter += sz
#define ASM_WRITE(thing, type) outputFile.write(TO_CH_PT(thing), sizeof(type)); byteCounter += sizeof(type)

int vm::assembler::assemble(const char* const& assemblyPath, const char* const& outputPath, AssemblerSettings& assemblerSettings) {
	cout << IO_MAIN "Attempting to assemble file \"" << assemblyPath << "\" into output file \"" << outputPath << "\"\n" IO_NORM;

	std::fstream assemblyFile, outputFile;

	assemblyFile.open(assemblyPath, std::ios::in);
	if (!assemblyFile.is_open()) {
		cout << IO_ERR "Could not open file \"" << assemblyPath << "\"" IO_NORM IO_END;
		return 1;
	}

	outputFile.open(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outputFile.is_open()) {
		cout << IO_ERR "Could not open file \"" << outputPath << "\"" IO_NORM IO_END;
		return 1;
	}

	try {
		int out = vm::assembler::assemble_(assemblyFile, outputFile, assemblerSettings, std::cout);
		cout << IO_MAIN "Assembly finished with code: " << out << IO_NORM IO_END;
		return out;
	} catch (AssemblerException& e) {
		cout << IO_ERR "Error during assembly at LINE " << e.line << ", COLUMN " << e.column << " : " << e.what() << IO_NORM IO_END;
	} catch (std::exception& e) {
		cout << IO_ERR "An unknown error ocurred during assembly. This error is most likely an issue with the c++ assembler code, not your code. Sorry. The provided error message is as follows:\n" << e.what() << IO_NORM IO_END;
	}

	return 1;
}

int vm::assembler::disassemble(const char* const& path) {
	cout << IO_MAIN "Attempting to disassemble file \"" << path << "\" to cout\n" IO_NORM;

	std::fstream file;
	file.open(path, std::ios::in | std::ios::binary);

	if (!file.is_open()) {
		cout << IO_ERR "Could not open file \"" << path << "\"" IO_NORM IO_END;
		return 1;
	}

	int out = vm::assembler::disassemble_(file, std::cout);
	cout << IO_MAIN "Disassembly finished with code: " << out << IO_NORM IO_END;

	return out;
}

int vm::assembler::disassemble_(std::iostream& bytecodeFile, std::ostream& stream) {
	using namespace types;
	using namespace opcode;
	
	Bytecode bytecode(bytecodeFile);
	bytecode.goto_(format::FIRST_INSTR_ADDR_LOCATION);
	bytecode.goto_(*reinterpret_cast<types::word_t*>(bytecode.ip));

	opcode_t opcode = 0;
	reg_t rid1 = 0;
	word_t word = 0;
	byte_t byte = 0;

	while (bytecode.ip < bytecode.end) {
		bytecode.read<opcode_t>(&opcode);
		stream << strings[opcode] << " ";
		for (const int& arg : args[opcode]) {
			switch (static_cast<ArgType>(arg)) {
				case ArgType::ARG_WORD_REG:
					stream << "Word-Reg ";
					bytecode.read<reg_t>(&rid1);
					break;
				case ArgType::ARG_BYTE_REG:
					stream << "Byte-Reg ";
					bytecode.read<reg_t>(&rid1);
					break;
				case ArgType::ARG_WORD:
					stream << "Word ";
					bytecode.read<word_t>(&word);
					break;
				case ArgType::ARG_BYTE:
					stream << "Byte ";
					bytecode.read<byte_t>(&byte);
					break;
			}
		}
		stream << "\n";
	}

	return 0;
}

int vm::assembler::assemble_(std::iostream& assemblyFile, std::iostream& outputFile, AssemblerSettings& assemblerSettings, std::ostream& stream) {
	using namespace opcode;
	using namespace types;

	// Flags/settings
	const bool isDebug = assemblerSettings.flags.hasFlags(FLAG_DEBUG);

	// File setup
	assemblyFile.clear();
	assemblyFile.seekg(0, std::ios::beg);
	outputFile.clear();
	outputFile.seekp(0, std::ios::beg);
	const std::streampos assemblyFileBeg = assemblyFile.tellg();
	const std::streampos outputFileBeg = outputFile.tellp();
	int byteCounter = 0;

	// String
	char str[vm::assembler::MAX_STR_SIZE];
	int strlen = 0;
	char c = -1;
	int line = 0;
	int column = -1;

	// Booleans
	bool end = false;
	bool isComment = false;
	bool isParen = false;
	bool isStr = false;
	bool isEscaped = false;
	bool isSettingGlobals = true;

	// Labels and Vars
	struct Label {
		struct Ref {
			const std::streampos pos;
			const int line;
			const int column;

			Ref(std::streampos posIn, int lineIn, int columnIn) : pos(posIn), line(lineIn), column(columnIn) {}
		};
		word_t val;
		std::vector<Ref> refs;
		bool isDef;

		Label() : val(0), isDef(false) {}
		Label(types::word_t valIn) : val(valIn), isDef(true) {}
	};
	std::unordered_map<std::string, Label> labels;
	std::string startstr = "@__START__";
	labels[startstr].isDef = true;
	labels[startstr].val = format::GLOBAL_TABLE_LOCATION; // TODO : set this later, to point after global data
	labels[startstr].refs.push_back(Label::Ref(outputFileBeg + static_cast<std::streamoff>(format::FIRST_INSTR_ADDR_LOCATION), -1, -1));
	word_t labelPlaceholder = wordPlaceholder;
	word_t labelErr = wordErr;

	// Lonely stdstr
	std::string stdstr;

	// Arguments, opcodes
	int carg = 0;
	opcode_t opcode = NOP;

	// Dummy values
	reg_t reg = 0;
	word_t word = 0;
	byte_t byte = 0;

	// Pre-assembly file writing
	word_t dummy = labelErr;
	ASM_WRITE(dummy, word_t); // First instruction addr (to be overwritten later)

	// Da big loop
	while (!end) {
		// Update char and file pos
		if (c == '\n') {
			line++;
			column = 0;
		} else {
			column++;
		}
		assemblyFile.get(c);
		if (assemblyFile.eof()) end = true;

		// Dealing with strings and comments
		if (isStr) {
			if (isEscaped) {
				if (c == 'n') {
					c = '\n';
					line--;
				} else if (c == 'c') {
					c = '\033';
				} else if (c == '0') {
					c = '\0';
				}
				// nothing on c == '"'

				isEscaped = false;
			} else {
				if (c == '"') {
					isStr = false;
					continue;
				}

				if (c == '\\') {
					isEscaped = true;
					continue;
				}
			}

			if (strlen >= MAX_STR_SIZE) {
				throw AssemblerException(AssemblerException::STRING_TOO_LONG, line, column);
			}
			str[strlen++] = c;
			continue;
		} else {
			if (c == '"') {
				isStr = true;
				isEscaped = false;
				continue;
			}

			if (isComment) {
				if (c == '\n') isComment = false;
				continue;
			}

			if (isParen) {
				if (c == ')') isParen = false;
				continue;
			}

			if (c == ';') {
				isComment = true;
				continue;
			}

			if (c == '(') {
				isParen = true;
				continue;
			}
		}

		// At the end of a token:
		if (c == ' ' || c == ',' || c == '\n' || c == '\t' || end) {
			if (strlen == 0) continue;
			str[strlen] = '\0';
			if (opcode == NOP) ASM_DEBUG("");
			ASM_DEBUG((opcode == NOP ? ":: " : "|  ") << str);

			// ~~~~~~~~~~~~~~~~~~~~~~~~~~~

			if (opcode == NOP) {
				if (str[0] == '@') {
					stdstr = str;
					labels[stdstr].val = byteCounter;
					labels[stdstr].isDef = true;
					ASM_DEBUG("Label Location: " << byteCounter);
				} else {
					opcode = stringMatchAt(str, strings, count);
					if (opcode == INVALID) { // opcode == 255, meaning stringMatchAt returned -1
						throw AssemblerException(AssemblerException::INVALID_OPCODE_PARSE, line, column);
					}

					if (isSettingGlobals && opcode < GLOBAL_BREAK) {
						isSettingGlobals = false;
						labels[startstr].val = byteCounter;
					}

					if (!isSettingGlobals) {
						if (opcode >= GLOBAL_BREAK) {
							throw AssemblerException(AssemblerException::MISPLACED_GLOBAL, line, column);
						} else {
							ASM_WRITE(opcode, opcode_t);
						}
					}

					carg = 0;
					ASM_DEBUG("Opcode: " << static_cast<int>(opcode));
					if (args[opcode][0] == 0) opcode = NOP;
				}

			} else {
				switch (args[opcode][carg]) {
					case 0: // ARG_NONE
						// THIS SHOULD NEVER BE CALLED
						carg = MAX_ARGS;
						break;

					case 1: // ARG_WORD_REG
						reg = parseWordRegister(str, strlen, line, column);
						ASM_WRITE(reg, reg_t);
						break;
					
					case 2: // ARG_BYTE_REG
						reg = parseByteRegister(str, strlen, line, column);
						ASM_WRITE(reg, reg_t);
						break;

					case 3: // ARG_WORD
						if (str[0] == '@' || str[0] == '%') {
							stdstr = str;
							labels[stdstr].refs.push_back(Label::Ref(outputFile.tellp(), line, column));
							ASM_WRITE(labelPlaceholder, word_t);
						} else {
							// TODO : printed wrong line/column?
							word = parseNumber<word_t, AssemblerException::INVALID_WORD_PARSE>(str, strlen, line, column);
							ASM_WRITE(word, word_t);
						}
						break;

					case 4: // ARG_BYTE
						byte = parseNumber<byte_t, AssemblerException::INVALID_BYTE_PARSE>(str, strlen, line, column);
						ASM_WRITE(byte, byte_t);
						break;

					case 5: // ARG_VAR
						if (str[0] == '%') {
							stdstr = str;
							labels[stdstr].val = byteCounter;
							labels[stdstr].isDef = true;
							ASM_DEBUG("Var Location: " << byteCounter);
						} else {
							throw AssemblerException(AssemblerException::INVALID_VAR_PARSE, line, column);
						}
						break;

					case 6: // ARG_STR
						ASM_WRITERAW(str, strlen + 1);
						break;
				}

				carg++;
				if (carg >= MAX_ARGS || args[opcode][carg] == 0) {
					opcode = NOP;
				}
			}

			// ~~~~~~~~~~~~~~~~~~~~~~~~~~~

			strlen = 0;
			continue;
		}

		// Add char to string
		if (strlen >= MAX_STR_SIZE) {
			throw AssemblerException(AssemblerException::STRING_TOO_LONG, line, column);
		}
		str[strlen++] = c;
	}

	/*
	opcode = HALT;
	ASM_WRITE(opcode, opcode_t);
	*/

	for (const std::pair<std::string, Label>& pair : labels) {
		if (pair.second.isDef) {
			for (const Label::Ref& ref : pair.second.refs) {
				outputFile.seekp(ref.pos);
				outputFile.write(reinterpret_cast<char*>(const_cast<word_t*>(&pair.second.val)), sizeof(word_t));
			}
		} else {
			if (pair.first[0] == '@') {
				throw AssemblerException(AssemblerException::UNDEFINED_LABEL, pair.second.refs[0].line, pair.second.refs[0].column, pair.first);
			} else {
				throw AssemblerException(AssemblerException::UNDEFINED_VAR, pair.second.refs[0].line, pair.second.refs[0].column, pair.first);
			}
		}
	}

	ASM_DEBUG(IO_END);

	return 0;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Parsing

types::reg_t vm::assembler::parseWordRegister(char* const& str, const int& strlen, const int& line, const int& column) {
	if (strlen < 2) throw AssemblerException(AssemblerException::INVALID_WORD_REG_PARSE, line, column);

	int match = stringMatchAt(str, register_::strings, register_::W0);

	if (match >= 0) {
		if (match == register_::PP || match == register_::BP) return match;
		else throw AssemblerException(AssemblerException::INVALID_WORD_REG_PARSE, line, column);
	} else if (str[0] == 'W') {
		types::reg_t out = parseNumber<types::reg_t, AssemblerException::INVALID_WORD_REG_PARSE>(str + 1, strlen - 1, line, column);

		if (out >= register_::NUM_WORD_REGISTERS || out < 0) {
			throw AssemblerException(AssemblerException::INVALID_WORD_REG_PARSE, line, column);
		}

		return out + register_::W0;
	}

	throw AssemblerException(AssemblerException::INVALID_WORD_REG_PARSE, line, column);
}

types::reg_t vm::assembler::parseByteRegister(char* const& str, const int& strlen, const int& line, const int& column) {
	if (strlen < 2) throw AssemblerException(AssemblerException::INVALID_BYTE_REG_PARSE, line, column);

	int match = stringMatchAt(str, register_::strings, register_::W0);

	if (match >= 0) {
		if (match == register_::FZ) return match;
		else throw AssemblerException(AssemblerException::INVALID_BYTE_REG_PARSE, line, column);
	} else if (str[0] == 'B') {
		types::reg_t out = parseNumber<types::reg_t, AssemblerException::INVALID_BYTE_REG_PARSE>(str + 1, strlen - 1, line, column);

		if (out >= register_::NUM_WORD_REGISTERS || out < 0) {
			throw AssemblerException(AssemblerException::INVALID_BYTE_REG_PARSE, line, column);
		}

		return out + register_::B0;
	}

	throw AssemblerException(AssemblerException::INVALID_BYTE_REG_PARSE, line, column);
}

template<typename T, vm::assembler::AssemblerException::ErrorType eType>
T vm::assembler::parseNumber(const char* str, int strlen, const int& line, const int& column) {
	T base = 10;
	T mul = 1;

	if (str[0] == '-') {
		str++;
		strlen--;
		mul = -1;
	}

	if (strlen > 2 && str[0] == '0') {
		char t = str[1];
		if ((t < '0' || t > '9') && t != '.') {
			str += 2;
			strlen -= 2;
			switch (t) {
				case 'x':
					base = 16;
					break;

				case 'b':
					base = 2;
					break;

				case 'd':break;

				default:
					throw AssemblerException(eType, line, column);
			}
		}
	}

	T out = 0;
	bool isFloat = false;
	char c;
	while (strlen > 0) {
		c = *str;

		if (c == '.') {
			if (sizeof(T) != sizeof(float)) throw AssemblerException(eType, line, column);
			isFloat = true;
			strlen--;
			str++;
			break;
		}

		out *= base;

		if ('0' <= c && c <= '9') {
			if (c - '0' >= base) throw AssemblerException(eType, line, column);
			out += c - '0';
		} else if ('A' <= c && c <= 'Z') {
			if (c - 'A' + 10 >= base) throw AssemblerException(eType, line, column);
			out += c - 'A' + 10;
		} else if ('a' <= c && c <= 'z') {
			if (c - 'a' + 10 >= base) throw AssemblerException(eType, line, column);
			out += c - 'a' + 10;
		} else {
			throw AssemblerException(eType, line, column);
		}

		strlen--;
		str++;
	}


	float floatOut = 0;
	float floatBase = 1 / static_cast<float>(base);
	float place = floatBase;

	if (isFloat) {
		while (strlen > 0) {
			c = *str;

			if ('0' <= c && c <= '9') {
				if (c - '0' >= base) throw AssemblerException(eType, line, column);
				floatOut += (c - '0') * place;
			} else if ('A' <= c && c <= 'Z') {
				if (c - 'A' + 10 >= base) throw AssemblerException(eType, line, column);
				floatOut += (c - 'A' + 10) * place;
			} else if ('a' <= c && c <= 'z') {
				if (c - 'a' + 10 >= base) throw AssemblerException(eType, line, column);
				floatOut += (c - 'a' + 10) * place;
			} else {
				throw AssemblerException(eType, line, column);
			}

			place *= floatBase;

			strlen--;
			str++;
		}

		floatOut += static_cast<float>(out);
		floatOut *= mul;
		return *reinterpret_cast<T*>(&floatOut);
	}

	return out * mul;
}