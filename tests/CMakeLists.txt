# SPDX-License-Identifier: GPL-3.0-or-later
# One executable per test,
# registered with CTest. CI runs these.
qt_add_executable(test_reading test_reading.cpp)
target_link_libraries(test_reading PRIVATE Qt6::Test klr_core)
add_test(NAME test_reading COMMAND test_reading)

qt_add_executable(test_binding test_binding.cpp)
target_link_libraries(test_binding PRIVATE Qt6::Test klr_core)
add_test(NAME test_binding COMMAND test_binding)

qt_add_executable(test_aggregate test_aggregate.cpp)
target_link_libraries(test_aggregate PRIVATE Qt6::Test klr_core)
add_test(NAME test_aggregate COMMAND test_aggregate)

qt_add_executable(test_bucket test_bucket.cpp)
target_link_libraries(test_bucket PRIVATE Qt6::Test klr_core)
add_test(NAME test_bucket COMMAND test_bucket)

qt_add_executable(test_historysync test_historysync.cpp)
target_link_libraries(test_historysync PRIVATE Qt6::Test klr_core)
add_test(NAME test_historysync COMMAND test_historysync)

qt_add_executable(test_carestatus test_carestatus.cpp)
target_link_libraries(test_carestatus PRIVATE Qt6::Test klr_core)
add_test(NAME test_carestatus COMMAND test_carestatus)

qt_add_executable(test_liveness test_liveness.cpp)
target_link_libraries(test_liveness PRIVATE Qt6::Test klr_core)
add_test(NAME test_liveness COMMAND test_liveness)

qt_add_executable(test_axis test_axis.cpp)
target_link_libraries(test_axis PRIVATE Qt6::Test klr_core)
add_test(NAME test_axis COMMAND test_axis)

qt_add_executable(test_units test_units.cpp)
target_link_libraries(test_units PRIVATE Qt6::Test klr_core)
add_test(NAME test_units COMMAND test_units)

qt_add_executable(test_repository test_repository.cpp)
target_link_libraries(test_repository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_repository COMMAND test_repository)

qt_add_executable(test_sensorrepository test_sensorrepository.cpp)
target_link_libraries(test_sensorrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_sensorrepository COMMAND test_sensorrepository)

qt_add_executable(test_bindingrepository test_bindingrepository.cpp)
target_link_libraries(test_bindingrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_bindingrepository COMMAND test_bindingrepository)

qt_add_executable(test_carethresholdrepository test_carethresholdrepository.cpp)
target_link_libraries(test_carethresholdrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_carethresholdrepository COMMAND test_carethresholdrepository)

qt_add_executable(test_plantrepository test_plantrepository.cpp)
target_link_libraries(test_plantrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_plantrepository COMMAND test_plantrepository)

qt_add_executable(test_journalrepository test_journalrepository.cpp)
target_link_libraries(test_journalrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_journalrepository COMMAND test_journalrepository)

qt_add_executable(test_attachmentrepository test_attachmentrepository.cpp)
target_link_libraries(test_attachmentrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_attachmentrepository COMMAND test_attachmentrepository)

qt_add_executable(test_filestore test_filestore.cpp)
target_link_libraries(test_filestore PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_filestore COMMAND test_filestore)

qt_add_executable(test_catalogcsv test_catalogcsv.cpp)
target_link_libraries(test_catalogcsv PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_catalogcsv COMMAND test_catalogcsv)

qt_add_executable(test_catalogrepository test_catalogrepository.cpp)
target_link_libraries(test_catalogrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_catalogrepository COMMAND test_catalogrepository)

qt_add_executable(test_catalogthresholds test_catalogthresholds.cpp)
target_link_libraries(test_catalogthresholds PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_catalogthresholds COMMAND test_catalogthresholds)

qt_add_executable(test_migration test_migration.cpp)
target_link_libraries(test_migration PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_migration COMMAND test_migration)

qt_add_executable(test_changelog test_changelog.cpp)
target_link_libraries(test_changelog PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_changelog COMMAND test_changelog)

qt_add_executable(test_legacyimport test_legacyimport.cpp)
target_link_libraries(test_legacyimport PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_legacyimport COMMAND test_legacyimport)

qt_add_executable(test_backuptokens test_backuptokens.cpp)
target_link_libraries(test_backuptokens PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_backuptokens COMMAND test_backuptokens)

qt_add_executable(test_readingscsvexport test_readingscsvexport.cpp)
target_link_libraries(test_readingscsvexport PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_readingscsvexport COMMAND test_readingscsvexport)

qt_add_executable(test_backupserialize test_backupserialize.cpp)
target_link_libraries(test_backupserialize PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_backupserialize COMMAND test_backupserialize)

qt_add_executable(test_backupimport test_backupimport.cpp)
target_link_libraries(test_backupimport PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_backupimport COMMAND test_backupimport)

qt_add_executable(test_plantduplicator test_plantduplicator.cpp)
target_link_libraries(test_plantduplicator PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_plantduplicator COMMAND test_plantduplicator)

qt_add_executable(test_sensordeleter test_sensordeleter.cpp)
target_link_libraries(test_sensordeleter PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_sensordeleter COMMAND test_sensordeleter)

qt_add_executable(test_appcontext test_appcontext.cpp)
target_link_libraries(test_appcontext PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_appcontext COMMAND test_appcontext)

qt_add_executable(test_navigation test_navigation.cpp)
target_link_libraries(test_navigation PRIVATE Qt6::Test klr_gui klr_core)
add_test(NAME test_navigation COMMAND test_navigation)

qt_add_executable(test_plantcaremodel test_plantcaremodel.cpp)
target_link_libraries(test_plantcaremodel PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_plantcaremodel COMMAND test_plantcaremodel)

qt_add_executable(test_sensorstatusmodel test_sensorstatusmodel.cpp)
target_link_libraries(test_sensorstatusmodel PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_sensorstatusmodel COMMAND test_sensorstatusmodel)

qt_add_executable(test_readingingester test_readingingester.cpp)
target_link_libraries(test_readingingester PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_readingingester COMMAND test_readingingester)

qt_add_executable(test_alertcontroller test_alertcontroller.cpp)
target_link_libraries(test_alertcontroller PRIVATE Qt6::Test klr_gui klr_persistence klr_core klr_style)
add_test(NAME test_alertcontroller COMMAND test_alertcontroller)

qt_add_executable(test_seriesmodel test_seriesmodel.cpp)
target_link_libraries(test_seriesmodel PRIVATE Qt6::Test klr_gui klr_core)
add_test(NAME test_seriesmodel COMMAND test_seriesmodel)

qt_add_executable(test_carethresholdsmodel test_carethresholdsmodel.cpp)
target_link_libraries(test_carethresholdsmodel PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_carethresholdsmodel COMMAND test_carethresholdsmodel)

qt_add_executable(test_plantlisthealth test_plantlisthealth.cpp)
target_link_libraries(test_plantlisthealth PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_plantlisthealth COMMAND test_plantlisthealth)

qt_add_executable(test_careevaluation test_careevaluation.cpp)
target_link_libraries(test_careevaluation PRIVATE Qt6::Test klr_gui klr_persistence klr_core)
add_test(NAME test_careevaluation COMMAND test_careevaluation)

qt_add_executable(test_advertisement test_advertisement.cpp)
target_link_libraries(test_advertisement PRIVATE Qt6::Test klr_devices)
add_test(NAME test_advertisement COMMAND test_advertisement)

qt_add_executable(test_devices test_devices.cpp)
target_link_libraries(test_devices PRIVATE Qt6::Test klr_devices)
add_test(NAME test_devices COMMAND test_devices)

qt_add_executable(test_gattdecoders test_gattdecoders.cpp)
target_link_libraries(test_gattdecoders PRIVATE Qt6::Test klr_devices)
add_test(NAME test_gattdecoders COMMAND test_gattdecoders)

qt_add_executable(test_gattcommands test_gattcommands.cpp)
target_link_libraries(test_gattcommands PRIVATE Qt6::Test klr_devices)
add_test(NAME test_gattcommands COMMAND test_gattcommands)

qt_add_executable(test_historydecoders test_historydecoders.cpp)
target_link_libraries(test_historydecoders PRIVATE Qt6::Test klr_devices)
add_test(NAME test_historydecoders COMMAND test_historydecoders)

qt_add_executable(test_mibeaconauth test_mibeaconauth.cpp)
target_link_libraries(test_mibeaconauth PRIVATE Qt6::Test klr_devices)
add_test(NAME test_mibeaconauth COMMAND test_mibeaconauth)

qt_add_executable(test_syncstaterepository test_syncstaterepository.cpp)
target_link_libraries(test_syncstaterepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_syncstaterepository COMMAND test_syncstaterepository)

qt_add_executable(test_agentrepository test_agentrepository.cpp)
target_link_libraries(test_agentrepository PRIVATE Qt6::Test klr_persistence klr_core)
add_test(NAME test_agentrepository COMMAND test_agentrepository)

qt_add_executable(test_theme test_theme.cpp)
target_link_libraries(test_theme PRIVATE Qt6::Test klr_style klr_core)
add_test(NAME test_theme COMMAND test_theme)

qt_add_executable(test_settingsstore test_settingsstore.cpp)
target_link_libraries(test_settingsstore PRIVATE Qt6::Test klr_style klr_core)
add_test(NAME test_settingsstore COMMAND test_settingsstore)

qt_add_executable(test_format test_format.cpp)
target_link_libraries(test_format PRIVATE Qt6::Test klr_style klr_core)
add_test(NAME test_format COMMAND test_format)

qt_add_executable(test_journalformat test_journalformat.cpp)
target_link_libraries(test_journalformat PRIVATE Qt6::Test klr_gui klr_core)
add_test(NAME test_journalformat COMMAND test_journalformat)

qt_add_executable(test_karnessmessage test_karnessmessage.cpp)
target_link_libraries(test_karnessmessage PRIVATE Qt6::Test karness)
add_test(NAME test_karnessmessage COMMAND test_karnessmessage)

qt_add_executable(test_karnessaccumulator test_karnessaccumulator.cpp)
target_link_libraries(test_karnessaccumulator PRIVATE Qt6::Test karness)
add_test(NAME test_karnessaccumulator COMMAND test_karnessaccumulator)

qt_add_executable(test_sseparser test_sseparser.cpp)
target_link_libraries(test_sseparser PRIVATE Qt6::Test karness)
add_test(NAME test_sseparser COMMAND test_sseparser)

qt_add_executable(test_chatcompletionscodec test_chatcompletionscodec.cpp)
target_link_libraries(test_chatcompletionscodec PRIVATE Qt6::Test karness)
add_test(NAME test_chatcompletionscodec COMMAND test_chatcompletionscodec)

qt_add_executable(test_thinksplitter test_thinksplitter.cpp)
target_link_libraries(test_thinksplitter PRIVATE Qt6::Test karness)
add_test(NAME test_thinksplitter COMMAND test_thinksplitter)

qt_add_executable(test_openaicompatprovider test_openaicompatprovider.cpp
    mockhttpserver.h mockhttpserver.cpp karnessfixtures.h)
target_link_libraries(test_openaicompatprovider PRIVATE Qt6::Test karness)
add_test(NAME test_openaicompatprovider COMMAND test_openaicompatprovider)

qt_add_executable(test_anthropicprovider test_anthropicprovider.cpp
    mockhttpserver.h mockhttpserver.cpp karnessfixtures.h)
target_link_libraries(test_anthropicprovider PRIVATE Qt6::Test karness)
add_test(NAME test_anthropicprovider COMMAND test_anthropicprovider)

qt_add_executable(test_responsesprovider test_responsesprovider.cpp
    mockhttpserver.h mockhttpserver.cpp karnessfixtures.h)
target_link_libraries(test_responsesprovider PRIVATE Qt6::Test karness)
add_test(NAME test_responsesprovider COMMAND test_responsesprovider)

qt_add_executable(test_geminiprovider test_geminiprovider.cpp
    mockhttpserver.h mockhttpserver.cpp karnessfixtures.h)
target_link_libraries(test_geminiprovider PRIVATE Qt6::Test karness)
add_test(NAME test_geminiprovider COMMAND test_geminiprovider)

qt_add_executable(test_agentsession test_agentsession.cpp
    fakeprovider.h fakeprovider.cpp faketool.h)
target_link_libraries(test_agentsession PRIVATE Qt6::Test karness)
add_test(NAME test_agentsession COMMAND test_agentsession)

qt_add_executable(test_contextbuilder test_contextbuilder.cpp)
target_link_libraries(test_contextbuilder PRIVATE Qt6::Test klr_agent klr_persistence klr_core)
add_test(NAME test_contextbuilder COMMAND test_contextbuilder)

qt_add_executable(test_webcontent test_webcontent.cpp)
target_link_libraries(test_webcontent PRIVATE Qt6::Test klr_agent klr_persistence klr_core karness)
add_test(NAME test_webcontent COMMAND test_webcontent)

qt_add_executable(test_agenttools test_agenttools.cpp)
target_link_libraries(test_agenttools PRIVATE Qt6::Test klr_agent klr_persistence klr_core karness)
add_test(NAME test_agenttools COMMAND test_agenttools)

qt_add_executable(test_transcript test_transcript.cpp)
target_link_libraries(test_transcript PRIVATE Qt6::Test klr_agent klr_persistence klr_core karness)
add_test(NAME test_transcript COMMAND test_transcript)

qt_add_executable(test_confirmingtool test_confirmingtool.cpp faketool.h)
target_link_libraries(test_confirmingtool PRIVATE Qt6::Test klr_agent karness)
add_test(NAME test_confirmingtool COMMAND test_confirmingtool)

qt_add_executable(test_agentviewmodel test_agentviewmodel.cpp fakeprovider.h fakeprovider.cpp)
target_link_libraries(test_agentviewmodel PRIVATE Qt6::Test klr_gui klr_agent klr_persistence klr_core karness)
add_test(NAME test_agentviewmodel COMMAND test_agentviewmodel)
