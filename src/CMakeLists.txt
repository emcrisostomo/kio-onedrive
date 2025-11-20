include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

add_subdirectory(integration)

set(kio_onedrive_SRCS
    kio_gdrive.cpp
    pathcache.cpp
    abstractaccountmanager.cpp
    gdriveurl.cpp
    onedriveclient.cpp)

set(BACKEND_SRC kaccountsmanager.cpp)
set(BACKEND_HEADER kaccountsmanager.h)
set(BACKEND_CLASSNAME KAccountsManager)
set(BACKEND_LIBS KAccounts6)

set(kio_onedrive_SRCS ${kio_onedrive_SRCS} ${BACKEND_SRC})
configure_file(gdrivebackend.h.in ${CMAKE_CURRENT_BINARY_DIR}/gdrivebackend.h)

ecm_qt_declare_logging_category(kio_onedrive_SRCS
    HEADER onedrivedebug.h
    IDENTIFIER ONEDRIVE
    CATEGORY_NAME kf.kio.workers.onedrive)

kcoreaddons_add_plugin(kio_onedrive
    SOURCES ${kio_onedrive_SRCS}
    INSTALL_NAMESPACE kf6/kio)

target_link_libraries(kio_onedrive
    Qt6::Core
    Qt6::Network
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::I18n
    ${BACKEND_LIBS})

set_target_properties(kio_onedrive PROPERTIES OUTPUT_NAME "onedrive")
