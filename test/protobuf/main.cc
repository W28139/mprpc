#include<iostream>
#include"test.pb.h"
using namespace fixbug;

int main()
{
    // LoginResponse rsp;
    // ResultCode* rc = rsp.mutable_result();
    // rc->set_errcode(1);
    // rc->set_esrrmsg("失败了");
    
    GetFriendListsResponse rsp;
    ResultCode *rc = rsp.mutable_result();
    rc->set_errcode(0);
    rc->set_esrrmsg("");

    User* user1 = rsp.add_firendlist();
    user1->set_name("aaa");
    user1->set_age(20);
    user1->set_sex(User::MAN);
    User* user2 = rsp.add_firendlist();
    user2->set_name("aaa");
    user2->set_age(20);
    user2->set_sex(User::MAN);

    std::cout<<rsp.firendlist_size()<<std::endl;

    return 0;
}
