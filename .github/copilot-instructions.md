# Copilot Instructions

This repository follows the OpenBMC C++ Coding Style and Conventions:
<https://github.com/openbmc/docs/blob/master/cpp-style-and-conventions.md>

---

## General Guidelines (All Copilot Features)

- Follow OpenBMC C++ style guide for all suggestions and completions
- Use C++23 standard and modern C++ features
- Apply RAII principles for resource management
- Prioritize code clarity through good naming and structure over comments
- Suggest refactoring for unclear code rather than adding explanatory comments

---

## Code Completion & Chat

- Generate code following OpenBMC naming and formatting conventions
- Suggest modern C++ features (auto, range-based loops, move semantics, etc.)
- Use appropriate patterns for error handling and logging

---

## Code Review Specific

### Focus Areas

- **Style conformance:** Verify adherence to OpenBMC style guide
- **Modern C++:** Check C++23 compliance and C++ Core Guidelines patterns
- **Memory safety:** RAII compliance, smart pointers, no resource leaks
- **Error handling:** Exception safety, input validation, robust error paths
- **Security:** Buffer overflows, input validation, privilege checks
- **Performance:** Consider embedded constraints; avoid unnecessary
  copies/allocations
- **Testing:** Suggest unit tests for new functionality

### Review Approach

- Provide specific, actionable feedback with code examples when helpful
- Explain reasoning behind recommendations
- Highlight effective patterns for reuse
- Focus on issues that affect maintainability and correctness
- Prioritize functional and safety issues over minor formatting inconsistencies
