#include"rpcprovider.h"
#include"rpcheader.pb.h"
#include"logger.h"

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;

    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    // 获取服务的名字
    std::string service_name = pserviceDesc->name();
    // 获取服务对象service的方法的数量
    int methodCnt = pserviceDesc->method_count();

    for(int i=0;i<methodCnt;i++)
    {
        // 获取了服务对象指定下标的服务方法的描述（抽象描述）
        const google::protobuf::MethodDescriptor* pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert({method_name,pmethodDesc});
    }
    service_info.m_service=service;
    m_serviceMap.insert({service_name,service_info});
}

void RpcProvider::Run()
{
    std::string ip =  MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    muduo::net::InetAddress address(ip,port);

    // 创建TcpServer对象
    muduo::net::TcpServer server(&m_eventLoop,address,"RpcProvider");

    // 绑定连接回调和消息读写回调的方法
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection,this,std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage,this,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));

    // 设置muduo库的线程数量
    server.setThreadNum(4);

    // 启动网络服务
    server.start();
    std::cout<<"start "<<ip<<"in "<<port<<std::endl;
    m_eventLoop.loop();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn)
{
    if(!conn->connected())
    {
        // 和 rpc client 连接断开了
        conn->shutdown();
    }
}



void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time)
{
    // 1. 从网络缓冲区获取全部原始字节流
    // 这段字节流包含了：[header_size] + [RpcHeader序列化串] + [args参数序列化串]
    std::string recv_buf = buf->retrieveAllAsString();

    // 2. 提取 header_size（头长度）
    // 我们人为规定：前4个字节以二进制形式存储 RpcHeader 的长度，用于解决 TCP 粘包问题。
    uint32_t header_size = 0;
    if (recv_buf.size() >= 4) 
    {
        recv_buf.copy((char*)&header_size, 4, 0);
    } 
    else 
    {
        // 数据包过短，逻辑异常处理
        return;
    }

    // 3. 根据 header_size 截取对应的二进制流，反序列化为 RpcHeader 对象
    // RpcHeader 包含了：service_name, method_name, 以及真正的参数长度 args_size
    // 这些字段对应我们在 rpcheader.proto 中的定义
    std::string rpc_header_str = recv_buf.substr(4, header_size);
    mprpc::RpcHeader rpcHeader;
    
    std::string service_name;
    std::string method_name;
    uint32_t args_size;

    if (rpcHeader.ParseFromString(rpc_header_str))
    {
        // 数据头反序列化成功，提取出后续解析业务参数所需的关键信息
        service_name = rpcHeader.service_name(); // 对应 proto 中的 bytes service_name
        method_name = rpcHeader.method_name();   // 对应 proto 中的 bytes method_name
        args_size = rpcHeader.args_size();       // 对应 proto 中的 uint32 args_size
    }
    else
    {
        // 如果 RpcHeader 反序列化失败，说明协议格式不对
        std::cout << "rpc_header_str parse error!" << std::endl;
        return;
    }

    // 4. 提取真正的业务参数二进制流
    // 起始位置 = 4字节长度位 + header_size长度位
    // 长度 = args_size（刚才从 rpcHeader 中解析出来的）
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    // 至此，
    // - 我们知道了要找哪个服务 (service_name)
    // - 知道了要调哪个方法 (method_name)
    // - 拿到了该方法对应的参数数据 (args_str)
    // ... 后续逻辑：去 m_serviceMap 找对应的 service 和 method ...
    // 打印调试信息
    std::cout << "================================================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "================================================" << std::endl;

    // 获取service对象和method对象
    auto it = m_serviceMap.find(service_name);
    if(it == m_serviceMap.end())
    {
        std::cout<<service_name<<"is not exit"<<std::endl;
        return;
    }
    
    auto mit = it->second.m_methodMap.find(method_name);
    if(mit == it->second.m_methodMap.end())
    {
        std::cout<<method_name<<"is not exit"<<std::endl;
        return;
    }

    google::protobuf::Service *service = it->second.m_service;      // 获取service对象
    const google::protobuf::MethodDescriptor *method = mit->second; // 获取method方法

    // 至此，我们已经拿到了具体的服务对象 (service) 和具体的方法描述符 (method)
    // 接下来需要把网络传来的二进制参数 (args_str) 转化成业务层能直接使用的对象。

    // 1. 根据方法描述符，动态创建一个该方法对应的“请求对象” (Request)
    // service->GetRequestPrototype(method) 会返回该方法要求的请求类型的一个“原型” (空对象)
    // .New() 则利用 C++ 的原型模式，在堆上创建一个该类型的具体实例。
    // 比如：如果 method 是 Login，那么这里创建出来的就是 LoginRequest 对象。
    // 注意：这里用父类指针 Message* 指向它，体现了框架的通用性，不需要包含具体的头文件。
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();

    // 2. 将之前解析出来的 args_str（参数字节流）反序列化到刚创建的 request 对象中
    // 这一步之后，request 对象里就填满了客户端传来的业务参数（如用户名、密码等）
    if (!request->ParseFromString(args_str))
    {
        std::cout << "request parse error, content: " << args_str << std::endl;
        return;
    }

    // 3. 同样的道理，动态创建一个该方法对应的“响应对象” (Response)
    // 比如：如果 method 是 Login，那么这里创建出来的就是 LoginResponse 对象。
    // 业务层（UserService）在执行完逻辑后，会将结果填入这个 response 对象中。
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    // 4. 最后写回调函数done
    // 为 CallMethod 绑定一个回调函数（Closure）
    // 这里的逻辑很巧妙：当业务层（如 UserService::Login）处理完业务后，会调用 done->Run()
    // 这个 Run() 实际上执行的就是我们这里绑定的 SendRpcResponse 方法。
    // 我们需要把当前的连接 (conn) 和 响应对象 (response) 传给这个回调函数。
    // 这个是模板函数，需要手写<>/自动推导传参类型
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider, 
                                                                  const muduo::net::TcpConnectionPtr&, 
                                                                  google::protobuf::Message*>
                                                                  (this, 
                                                                   &RpcProvider::SendRpcResponse, 
                                                                   conn, response);

    // 在框架上根据远端 RPC 请求，调用当前 RPC 节点上发布的方法
    // 这里的 service 就是我们之前注册的业务对象，CallMethod 会通过多态跳进业务层重写的函数里
    service->CallMethod(
        method,   // 方法描述符
        nullptr,  // RpcController，这里先传 nullptr，以后可以扩展控制逻辑
        request,  // 刚才反序列化好的请求参数
        response, // 刚才创建的空响应对象
        done      // 我们刚刚绑定的回调函数
    );
}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response)
{
    std::string response_str;
    
    // 1. 将 response 对象序列化成字节流
    if (response->SerializeToString(&response_str))
    {
        // 序列化成功，通过 muduo 网络库将字节流发回给 RPC 调用端（客户端）
        conn->send(response_str);
    }
    else
    {
        std::cout << "serialize response_str error!" << std::endl;
    }

    // 2. RPC 调用完成后，主动断开连接（短连接模拟）
    // 实际生产环境中可以根据 header 里的标识决定是否保持长连接
    conn->shutdown(); 
}