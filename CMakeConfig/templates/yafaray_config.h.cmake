#pragma once
// Header file generated by CMake please don't change it

// These preprocessor macros are set by cmake during building in file yafray_config.h.cmake
// They contain information about how the YafaRay package was built, CMake options used, compiler flags, etc

/****************************************************************************
*      This is part of the libYafaRay package
*
*      This library is free software; you can redistribute it and/or
*      modify it under the terms of the GNU Lesser General Public
*      License as published by the Free Software Foundation; either
*      version 2.1 of the License, or (at your option) any later version.
*
*      This library is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*      Lesser General Public License for more details.
*
*      You should have received a copy of the GNU Lesser General Public
*      License along with this library; if not, write to the Free Software
*      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef Y_CONFIG_H
#define Y_CONFIG_H

// Some important ray tracing parameters set in CMake
#define MIN_RAYDIST @YAF_MIN_RAY_DIST@
#define YAF_SHADOW_BIAS @YAF_SHADOW_BIAS@

// CMake checks for the unistd.h header file
#cmakedefine HAVE_UNISTD_H 1

// The YafaRay version displayed in the logs/badges can be set manually using a CMake variable as follows:
// For example: cmake -DYAFARAY_CORE_VERSION="v1.2.3"
// If the version is not manually set, it will use the information obtained from the latest Git commit/tag.
#define YAFARAY_BUILD_VERSION std::string("@YAFARAY_CORE_VERSION@@DEBUG@")

// Several YafaRay build informative variables
#define YAFARAY_BUILD_ARCHITECTURE std::string("@YAFARAY_BUILD_ARCHITECTURE@")
#define YAFARAY_BUILD_COMPILER std::string("@YAFARAY_BUILD_COMPILER@")
#define YAFARAY_BUILD_OS std::string("@YAFARAY_BUILD_OS@")
#define YAFARAY_BUILD_PLATFORM std::string("@YAFARAY_BUILD_PLATFORM@")


#endif

