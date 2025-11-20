kcoreaddons_add_plugin(onedrivecontextmenuaction
    SOURCES contextmenuaction.cpp
    INSTALL_NAMESPACE "kf6/kfileitemaction")

target_link_libraries(onedrivecontextmenuaction
    KF6::I18n
    KF6::KIOWidgets)
