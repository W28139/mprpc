# 编译用户服务客户端
add_executable(user_consumer calluserservice.cc ../user.pb.cc)
target_link_libraries(user_consumer mprpc protobuf)

# 编译好友服务客户端
add_executable(friend_consumer callfriendservice.cc ../friend.pb.cc)
target_link_libraries(friend_consumer mprpc protobuf)