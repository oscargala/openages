// Automatically generated for diff:
// @CURRENT_DIFF_HASH@

// 1 in a release build (PRODUCT_RELEASE_BUILD: cmake/BuildInfo.cmake), 0 otherwise.
#cmakedefine01 PLASMA_BUILD_INFO_NAMED_STRINGS

namespace plProduct
{
    constexpr uint32_t PRODUCT_BRANCH_ID = @PRODUCT_BRANCH_ID@;
    constexpr uint32_t PRODUCT_BUILD_ID = @PRODUCT_BUILD_ID@;
    constexpr uint32_t PRODUCT_BUILD_TYPE = @PRODUCT_BUILD_TYPE@;

    constexpr std::string_view PRODUCT_CORE_NAME = "@PRODUCT_CORE_NAME@";
    constexpr std::string_view PRODUCT_SHORT_NAME = "@PRODUCT_SHORT_NAME@";
    constexpr std::string_view PRODUCT_LONG_NAME = "@PRODUCT_LONG_NAME@";
    constexpr std::string_view PRODUCT_UUID = "@PRODUCT_UUID@";

#if PLASMA_BUILD_INFO_NAMED_STRINGS
    // A release build's commit and tag are arrays with external linkage, not string literals. The linker merges the
    // string literals of the whole client into one pool ordered by their endings, so a literal's place in the pool,
    // and with it the addresses of the strings after it, would follow its text. An internal array would join the pool
    // too: the optimizer marks a constant whose address is never compared unnamed_addr, which makes it mergeable. An
    // external one keeps a segment of its own, as long as plProduct.cpp is compiled without LTO, as a release build
    // compiles it (Sources/Plasma/CoreLib/CMakeLists.txt): under LTO the linker would make the hidden symbol internal,
    // and it could join the pool again. Only plProduct.cpp may include this file: a second includer would define the
    // arrays twice, which a release build's link refuses.
    extern constexpr char GIT_REV_CHARS[] = "@GIT_REV@";
    extern constexpr char GIT_TAG_CHARS[] = "@GIT_TAG@";
    constexpr std::string_view GIT_REV{GIT_REV_CHARS, sizeof(GIT_REV_CHARS) - 1};
    constexpr std::string_view GIT_TAG{GIT_TAG_CHARS, sizeof(GIT_TAG_CHARS) - 1};
#else
    constexpr std::string_view GIT_REV = "@GIT_REV@";
    constexpr std::string_view GIT_TAG = "@GIT_TAG@";
#endif

    constexpr std::string_view VOLATILE_BUILD_DATE = "@BUILD_DATE@";
    constexpr std::string_view VOLATILE_BUILD_TIME = "@BUILD_TIME@";
};
