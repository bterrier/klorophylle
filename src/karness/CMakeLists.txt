# SPDX-License-Identifier: GPL-3.0-or-later
# karness — generic LLM agent harness (docs/adr/0019 §1). Extractable: QtCore +
# QtNetwork + C++23 ONLY — no QtQml/Quick/Sql, no klr_*. Builds both as an
# add_subdirectory() child of klorophylle and STANDALONE (the CI extraction
# gate configures this directory as a top-level project with nothing but Qt
# on the prefix path, keeping "extractable one day" mechanically true).
cmake_minimum_required(VERSION 3.21)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # Standalone mode (extraction gate).
    project(karness VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Core Network)
    qt_standard_project_setup(REQUIRES 6.10)
    set(CMAKE_AUTOMOC ON)
endif()

qt_add_library(karness STATIC
    agenterror.h
    message.h
    messagecodec.h
    messagecodec.cpp
    streamevent.h
    messageaccumulator.h
    messageaccumulator.cpp
    sseparser.h
    sseparser.cpp
    decodedchunk.h
    dialect.h
    providerconfig.h
    streamingprovider.h
    streamingprovider.cpp
    reasoningbudget.h
    chatcompletionscodec.h
    chatcompletionscodec.cpp
    chatcompletionsdialect.h
    chatcompletionsdialect.cpp
    anthropiccodec.h
    anthropiccodec.cpp
    anthropicdialect.h
    anthropicdialect.cpp
    anthropicprovider.h
    responsescodec.h
    responsescodec.cpp
    responsesdialect.h
    responsesdialect.cpp
    responsesprovider.h
    geminicodec.h
    geminicodec.cpp
    geminidialect.h
    geminidialect.cpp
    geminiprovider.h
    thinktagsplitter.h
    thinktagsplitter.cpp
    openaicompatprovider.h
    openaicompatprovider.cpp
    toolspec.h
    modelcaps.h
    inferencerequest.h
    iprovider.h
    itool.h
    agentsession.h
    agentsession.cpp
)
target_link_libraries(karness PUBLIC Qt6::Core Qt6::Network)
target_compile_features(karness PUBLIC cxx_std_23)
target_include_directories(karness PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
