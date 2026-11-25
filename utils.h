#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#define ASSERT(cond, ...)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "%s:%d:%s: FAILED ASSERT `%s`", \
                __FILE__, __LINE__, __func__, #cond);       \
        	__VA_OPT__(fprintf(stderr, ": " __VA_ARGS__);)  \
        	fprintf(stderr, "\n");                          \
            abort();                                        \
        }                                                   \
    } while (0)

static inline int16_t clamp16(int32_t x){
	constexpr int size16 = 32767;
	if(x >  size16) return  size16;
	if(x < -size16) return -size16;
	return x;
}

static inline float lerpf(float from, float to, float weight){
	return from + (to - from) * weight;
}

static float random_step(float variation){
	if(variation <= 0.0f) return 1.0f;
	auto random = (float)rand() / (float)RAND_MAX;
	auto ranged_rand = (random * 2.0f - 1.0f);
	return 1.0f + ranged_rand * variation;
}

#endif //_UTILS_H
