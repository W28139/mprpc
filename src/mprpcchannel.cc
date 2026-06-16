#include"mprpcchannel.h"
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include "rpcheader.pb.h"
#include"mprpcapplication.h"

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                google::protobuf::RpcController* controller, 
                                const google::protobuf::Message* request,
                                google::protobuf::Message* response, 
                                google::protobuf::Closure* done)
{
    const google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();

    // 1. 获取参数的序列化字符串长度 args_size
    uint32_t args_size = 0;
    std::string args_str;
    // 进行序列化
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialize request error!");
        return;
    }

    // 2. 定义并序列化 rpc 的请求 header
    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    uint32_t header_size = 0;
    std::string rpc_header_str;
    if (rpcHeader.SerializeToString(&rpc_header_str))
    {
        header_size = rpc_header_str.size();
    }
    else
    {
        controller->SetFailed("serialize rpc header error!");
        return;
    }

    // 3. 组织待发送的 rpc 请求的字符串
    // 格式：header_size(4字节) + rpc_header_str + args_str
    std::string send_rpc_str;
    send_rpc_str.insert(0, std::string((char*)&header_size, 4)); // 将长度以二进制形式存入前4字节
    send_rpc_str += rpc_header_str; // 头部
    send_rpc_str += args_str;        // 参数

    // 4. 使用 TCP 编程，完成 rpc 方法的远程调用 (这里通常使用同步阻塞模式)
    // 实际生产环境会从配置文件读取服务端 IP 和 Port
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        controller->SetFailed("create socket error! errno:" + std::to_string(errno));
        return;
    }

    // 这里假设通过某个配置类获取服务器地址
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port; // 示例端口
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str()); // 示例IP

    // 连接 rpc 服务节点
    if (-1 == connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr)))
    {
        close(clientfd);
        controller->SetFailed("connect error! errno:" + std::to_string(errno));
        return;
    }

    // 发送 rpc 请求
    if (-1 == send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
    {
        close(clientfd);
        controller->SetFailed("send error! errno:" + std::to_string(errno));
        return;
    }

    // 5. 接收 rpc 调用完成后的响应值
    char recv_buf[1024] = {0};
    int recv_size = 0;
    if (-1 == (recv_size = recv(clientfd, recv_buf, 1024, 0)))
    {
        close(clientfd);
        controller->SetFailed("recv error! errno:" + std::to_string(errno));
        return;
    }

    // 6. 反序列化 rpc 响应
    // 注意：这里 recv_buf 可能会有粘包问题，简单实现可以直接 ParseFromArray
    if (!response->ParseFromArray(recv_buf, recv_size))
    {
        close(clientfd);
        controller->SetFailed("parse error! response_str:" + std::string(recv_buf));
        return;
    }

    close(clientfd);
}