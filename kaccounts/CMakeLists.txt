find_package(KF6Notifications ${KF_MIN_VERSION} REQUIRED)
find_package(Intltool REQUIRED)

kaccounts_add_service(${CMAKE_CURRENT_SOURCE_DIR}/onedrive.service.in)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/onedrive.provider.in.in
    ${CMAKE_CURRENT_BINARY_DIR}/onedrive.provider.in @ONLY)
kaccounts_add_provider(${CMAKE_CURRENT_BINARY_DIR}/onedrive.provider.in NO_INTLTOOL)

kcoreaddons_add_plugin(kaccounts_onedrive
    SOURCES kaccountsplugin.cpp
    INSTALL_NAMESPACE kaccounts/daemonplugins)

set_target_properties(kaccounts_onedrive PROPERTIES OUTPUT_NAME "onedrive")
target_link_libraries(kaccounts_onedrive
    KAccounts6
    KF6::KIOWidgets
    KF6::I18n
    KF6::Notifications)

install(FILES onedrive.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
