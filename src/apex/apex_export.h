//  Copyright (c) 2014 University of Oregon
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef APEX_EXPORT_H
#define APEX_EXPORT_H

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#define APEX_EXPORT __declspec(dllexport)
#define APEX_WEAK
#define APEX_APPLE_WEAK
#else

#define APEX_EXPORT __attribute__((visibility("default")))

#ifdef __APPLE__
#define APEX_WEAK
#define APEX_APPLE_WEAK __attribute__((weak_import))
#else
#define APEX_WEAK __attribute__((weak))
#define APEX_APPLE_WEAK
#endif

#endif

#endif
