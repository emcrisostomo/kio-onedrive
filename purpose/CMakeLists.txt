kcoreaddons_add_plugin(purpose_onedrive SOURCES purpose_onedrive.cpp gdrivejob.cpp INSTALL_NAMESPACE "kf6/purpose")
target_link_libraries(purpose_onedrive Qt::Core KF6::Purpose)
set_target_properties(purpose_onedrive PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/kf6/purpose")
install(FILES "purpose_onedrive_config.qml" DESTINATION ${KDE_INSTALL_DATADIR}/purpose)

target_link_libraries(purpose_onedrive KF6::KIOCore KF6::I18n KF6::Purpose)
