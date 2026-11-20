#pragma once
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <string>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[ERRO CRITICO] Falha na assercao: " << (msg) \
                      << "\nCondicao: " << #cond \
                      << "\nArquivo: " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(false)
