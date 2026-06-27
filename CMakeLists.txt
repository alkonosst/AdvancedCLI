cmake_minimum_required(VERSION 3.14)
project(AdvancedCLI VERSION 0.6.0 LANGUAGES CXX)

# Add sources to the library target.
add_library(AdvancedCLI STATIC
  src/internal/AdvancedCLI.cpp
  src/internal/acli-argument.cpp
  src/internal/acli-command.cpp
)

# Alias with author prefix. Consumers link "alkonosst::AdvancedCLI".
add_library(alkonosst::AdvancedCLI ALIAS AdvancedCLI)

# PUBLIC (not INTERFACE): the include dir is needed by the library itself to compile its .cpp, AND
# also by whoever uses it.
target_include_directories(AdvancedCLI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Set a C++ standard requirement for the library. This will also propagate to consumers of the library.
target_compile_features(AdvancedCLI PUBLIC cxx_std_11)
