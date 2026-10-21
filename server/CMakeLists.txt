# 参考服务端（控制台程序）
add_executable(mock_server
    src/main.cpp
    src/MockServer.cpp
    src/MockServer.h
    src/ClientSession.cpp
    src/ClientSession.h
    src/PasswordHash.cpp
    src/PasswordHash.h
    src/SessionRegistry.cpp
    src/SessionRegistry.h
    src/store/Database.cpp
    src/store/Database.h
    src/store/UserRepository.cpp
    src/store/UserRepository.h
    src/store/FriendRepository.cpp
    src/store/FriendRepository.h
    src/store/MessageRepository.cpp
    src/store/MessageRepository.h
)

target_include_directories(mock_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(mock_server PRIVATE
    Qt6::Network
    Qt6::Sql
    protocol
)
