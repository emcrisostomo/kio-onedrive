set(onedrivepropertiesplugin_SRCS
    onedrivepropertiesplugin.cpp
    ../../onedrivedebug.cpp
)

ki18n_wrap_ui(onedrivepropertiesplugin_SRCS onedrivepropertiesplugin.ui)

kcoreaddons_add_plugin(onedrivepropertiesplugin
    SOURCES ${onedrivepropertiesplugin_SRCS}
    INSTALL_NAMESPACE "kf6/propertiesdialog")

target_link_libraries(onedrivepropertiesplugin
    KF6::I18n
    KF6::KIOWidgets
)
