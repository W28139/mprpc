#include<iostream>
#include"mprpcapplication.h"
#include"user.pb.h"
#include"mprpcchannel.h"
int main(int argc, char **argv)
{
    // 程序启动以后，想使用mprpc框架来享受rpc服务调用，需要先调用框架的初始化函数(初始化一次即可)
    MprpcApplication::Init(argc,argv);

    // 演示调用远程发布的rpc方法的Login
    fixbug::UserServiceRpc_Stub stub(new MprpcChannel());
    // rpc方法的请求参数
    fixbug::LoginRequest request;
    request.set_name("111");
    request.set_pwd("123456");
    // rpc方法的响应
    fixbug::LoginResponse response;

    // 发起rpc方法的调用，同步的rpc调用过程 
    // RpcChannel->RpcChannel::callMethod 集中做所有rpc方法调用参数序列化和网络发送
    stub.Login(nullptr,&request,&response,nullptr);

    // 一次rpc调用结束，读取调用结果
    if(response.result().errcode()==0)
    {
        std::cout<<"rpc login response success:"<<response.success()<<std::endl;
    }
    else
    {  
        std::cout<<"rpc login response error:"<<response.result().errmsg()<<std::endl;
    }
    return 0;
}