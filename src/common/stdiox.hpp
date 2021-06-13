
#ifndef INCLUDE_CHIA_STDIOX_HPP_
#define INCLUDE_CHIA_STDIOX_HPP_
#include <stdio.h>

#ifdef _WIN32
#define FOPEN(...) _wfopenX(__VA_ARGS__)
#define FSEEK(...) _fseeki64(__VA_ARGS__)

#include <locale>
#include <codecvt>
#include <string>
#include <iostream>

    inline FILE* _wfopenX(const wchar_t* filename, const wchar_t* mode) {
        FILE* file = NULL;
		_wfopen_s(&file, filename, mode);

        return file;
    }

#else
#define FOPEN(...) fopen(__VA_ARGS__)
#define FSEEK(...) fseek(__VA_ARGS__)
#endif


#endif  // INCLUDE_CHIA_STDIOX_HPP_