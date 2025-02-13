typedef char16_t WCHAR;
typedef unsigned short CHAR16_T;

size_t u16_strlen(const WCHAR* inputStr)
{
    auto str = (CHAR16_T*)inputStr;

    size_t len = 0;
    while (*str++) {
        len++;
    }

    return len;
}
