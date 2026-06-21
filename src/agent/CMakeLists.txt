# SPDX-License-Identifier: GPL-3.0-or-later
# klr_agent — WatchFlower-side AI agent integration (docs/adr/0019): the
# deterministic context block, the domain tools, and transcript persistence mapping.
qt_add_library(klr_agent STATIC
    agent.cpp
    contextbuilder.cpp
    agenttools.cpp
    webcontent.cpp
    networkwebfetcher.cpp
    transcript.cpp
    confirmingtool.cpp
    isecretstore.h
    inmemorysecretstore.h
    iwebfetcher.h
    fakewebfetcher.h
    networkwebfetcher.h
)
target_link_libraries(klr_agent PUBLIC karness klr_core klr_persistence)
target_include_directories(klr_agent PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
