# 共享协议库：客户端与 mock 服务端共用同一份帧格式与消息类型定义
add_library(protocol STATIC
    protocol/Frame.cpp
    protocol/Frame.h
    protocol/MessageType.h
    protocol/ProtocolVersion.h
)

# 对外暴露 shared/ 作为头文件根，使用方以 "protocol/xxx.h" 形式包含，避免与第三方头文件重名
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(protocol PUBLIC Qt6::Core)
