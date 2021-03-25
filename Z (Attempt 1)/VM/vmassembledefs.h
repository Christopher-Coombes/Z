#pragma once
#include "vm.h"

#define ASM_THROW(x) \
	throw vm::AssemblyError(vm::AssemblyError::x)

template<typename T>
bool parseNumber(T& num, char* str) {
	std::stringstream convStream;
	int16_t convChar;
	convStream.str("");
	convStream.clear();
	convStream << std::hex << str;
	if (sizeof(T) == 1) {
		if (!(convStream >> convChar))
			return false;
		num = static_cast<T>(convChar);
		return true;
	} else {
		return static_cast<bool>(convStream >> num);
	}
}

void parseOpcode(vm::types::opcode_t& opcode, char* const& str) {
	using namespace vm::Opcode;
	const char* curr;
	int j;
	for (opcode = 0; opcode < count; opcode++) {
		curr = strings[opcode];
		j = 0;
		while (true) {
			if (str[j] != curr[j])
				goto next;

			if (str[j++] == '\0')
				return;
		}
	next:;
	}

	ASM_THROW(UNKNOWN_OPCODE);
}

void parseRegister(vm::types::register_t& reg, char* const& str) {
	vm::types::register_t temp;
	if (str[0] == 'R') {
		if (!parseNumber<vm::types::register_t>(temp, str + 1)) {
			ASM_THROW(INVALID_REGISTER);
		}
		reg = temp + vm::Register::GP_REGISTER_OFFSET;
	} else if (str[0] == 'B' && str[1] == 'P' && str[2] == '\0') {
		reg = vm::Register::BP;
	} else if (str[0] == 'C' && str[1] == 'P' && str[2] == '\0') {
		reg = vm::Register::BP;
	}
}

void parseLiteral(vm::types::literal_t& lit, char* const& str) {
	if (!parseNumber<vm::types::literal_t>(lit, str)) {
		ASM_THROW(INVALID_LITERAL);
	}
}

void parseAddress(vm::types::address_t& lit, char* const& str) {
	if (!parseNumber<vm::types::address_t>(lit, str)) {
		ASM_THROW(INVALID_ADDRESS);
	}
}