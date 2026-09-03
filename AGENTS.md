# C++ Best Practices - Summary Guidelines

Standards and best practices for generating and veviewing cpp codes in this project.


## Core Principles

### ✅ DO

#### Naming Conventions
- **Class members**: Use `m` prefix + camelCase (e.g., `mUserName`, `mLastUpdateTime`)
- **Functions/variables**: Use camelCase (e.g., `calculateTotalPrice()`, `parseMarketData()`)
- **Constants/enums**: Use UPPER_CASE with underscores (e.g., `MAX_BUFFER_SIZE`, `ConnectionState::CONNECTED`)
- **Namespaces**: Use lowercase, can include underscores

#### Constants & Type Safety
- Use `constexpr` for compile-time constants
- Prefer `enum class` over plain enums
- Use `static_assert` for compile-time assertions
- Use scientific notation for large floats (e.g., `1e4` instead of `10000.0`)
- Use specific typed pointers/references - never `void*`
- Prefer strong typing (enums/classes) over raw integers

#### Memory Management
- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) instead of raw pointers
- Apply RAII pattern for automatic resource management
- Use `make_unique`/`make_shared` for creation
- Prefer stack allocation over heap when possible

#### Error Handling
- Return error codes for expected failures
- Use `std::optional` for operations that may fail without detailed error info
- Use `std::expected` for operations that may fail with detailed error info
- Use exceptions only for truly exceptional conditions (resource exhaustion, programming errors)
- Never throw exceptions in destructors

#### Parameter Passing
- Prefer references over pointers for parameters
- Use `const` references for read-only parameters
- Use pointers only when necessary (optional params, polymorphism)
- Use `std::span` for array data instead of pointer+size pairs

#### C++23 Features
- Use cpp standard library types to express precise semantics, allow compiler more understand source code
- Use concepts to add constraints to generic programming
- Use `requires` clauses to explicitly specify template requirements
- Use `std::span` to replace pointer+length parameter pairs

#### Performance Optimization
- Avoid unnecessary copies - use references or move semantics
- Use `constexpr` functions for compile-time computations
- Minimize allocations in hot paths

#### Code Organization
- Move all non-latency-critical implementations into `.cpp` files - keep headers minimal and declaration-only
- Use forward declarations to reduce header dependencies
- Use `final` keyword to mark non-inheritable classes/methods
- Use `override` keyword to mark virtual function overrides
- Follow PIMPL pattern for implementation hiding

#### Memory Buffer Handling
- Always verify boundary safety before reading/writing buffer memory
- Use `std::array<T, N>` for fixed-size buffers instead of raw `char[]` arrays
- Validate all buffer operations are within bounds

#### Logging Best Practices
- Use `std::print` for log output
- For array/char array data: use `std::string_view` + `strnlen()` to prevent buffer overflows
- For object logging: define `std::formatter<T>` specializations and use `std::format("{}", obj)` for clean code
- Prefer structured logging over raw string concatenation

### ❌ DON'T

#### Naming & Scope
- Avoid single-character variable names (except loop counters)

#### Performance Pitfalls
- Avoid repeated allocations in loops - reuse objects
- Avoid unnecessary type conversions (precision loss, performance impact)
- Don't overuse `auto` when type is not obvious from context

#### C++ Feature Misuse
- Avoid `std::bind`/`std::function` overuse - prefer lambdas
- Don't use `std::endl` - use `\n` to avoid unnecessary flushes
- Avoid `auto` with expressions when type is unclear

#### Critical Warnings
- Never use `delete this` - extremely dangerous
- Don't use exceptions for control flow - exceptions should be exceptional
- Don't access buffers without bounds checking
