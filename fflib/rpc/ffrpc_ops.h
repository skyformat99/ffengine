
#ifndef _FF_RPC_OPS_H_
#define _FF_RPC_OPS_H_

#include <assert.h>
#include <string>
using namespace std;

#include "base/ffslot.h"
#include "net/socket_i.h"
#include "base/fftype.h"
#include "net/codec.h"
#include "base/singleton.h"

#ifdef FF_ENABLE_PROTOCOLBUF //! 如果需要开启对protobuf的支持，需要开启这个宏
#include <google/protobuf/message.h>
#endif

#include <thrift/FFThrift.h>

#include "rpc/msg_def/ffrpc_msg_types.h"
#include "rpc/msg_def/ffrpc_msg_constants.h"

namespace apache
{
    namespace thrift{
        namespace protocol
        {
            class TProtocol;
        }
    }
}
namespace ff
{

//! 各个节点的类型
enum node_type_e
{
    BRIDGE_BROKER, //! 连接各个区服的代理服务器
    MASTER_BROKER, //! 每个区服的主服务器
    SLAVE_BROKER,  //! 从服务器
    RPC_NODE,      //! rpc节点
    SYNC_CLIENT_NODE, //! 同步调用client
};

#define BROKER_MASTER_NODE_ID   0
#define GEN_SERVICE_NAME(M, X, Y) snprintf(M, sizeof(M), "%s@%u", X, Y)
#define RECONNECT_TO_BROKER_TIMEOUT       1000//! ms
#define RECONNECT_TO_BROKER_BRIDGE_TIMEOUT       1000//! ms
    
class ffslot_msg_arg: public ffslot_t::callback_arg_t
{
public:
    ffslot_msg_arg(const string& s_, socket_ptr_t sock_):
        body(s_),
        sock(sock_)
    {}
    virtual int type()
    {
        return TYPEID(ffslot_msg_arg);
    }
    string       body;
    socket_ptr_t sock;
};

struct msg_tool_t
{
    static string encode(msg_i& msg_)
    {
        return msg_.encode_data();
    }
    template<typename T>
    static int decode(T& msg_, const string& data_, string* err_ = NULL)
    {
        try{
            //msg_.decode_data(data_);
            ffthrift_t::DecodeFromString(msg_, data_);
        }
        catch(exception& e)
        {
            if (err_) *err_ += e.what();
        }
        return 0;
    }

};

class ffresponser_t
{
public:
    virtual ~ffresponser_t(){}
    virtual void response(const string& dest_namespace_, const string& msg_name_,  uint64_t dest_node_id_,
                          int64_t callback_id_, const string& body_, string err = "") = 0;
};

class ffslot_req_arg: public ffslot_t::callback_arg_t
{
public:
    ffslot_req_arg(const string& s_, uint64_t n_, int64_t cb_id_, const string& name_space_, string err_info_, ffresponser_t* p):
        body(s_),
        dest_node_id(n_),
        callback_id(cb_id_),
        dest_namespace(name_space_),
        err_info(err_info_),
        responser(p)
    {}
    ffslot_req_arg(){}
    ffslot_req_arg(const ffslot_req_arg& src):
        body(src.body),
        dest_node_id(src.dest_node_id),
        callback_id(src.callback_id),
        dest_namespace(src.dest_namespace),
        err_info(src.err_info),
        responser(src.responser)
    {}
    ffslot_req_arg& operator=(const ffslot_req_arg& src)
    {
        body = src.body,
        dest_node_id = src.dest_node_id;
        callback_id = src.callback_id;
        dest_namespace = src.dest_namespace;
        err_info = src.err_info;
        responser = src.responser;
        return *this;
    }
    virtual int type()
    {
        return TYPEID(ffslot_req_arg);
    }
    string          body;
    uint64_t        dest_node_id;//! 请求来自于那个node id
    int64_t         callback_id;//! 回调函数标识id
    string          dest_namespace;
    string          err_info;
    ffresponser_t*  responser;
};



class null_type_t: public ffmsg_t<null_type_t>
{
    void encode(){}
    void decode(){}
};

template<typename IN, typename OUT = null_type_t>
struct ffreq_t
{
    ffreq_t():
        dest_node_id(0),
        callback_id(0),
        responser(NULL)
    {}
    bool error() const { return err_info.empty() == false; }
    const string& error_msg() const { return err_info; }
    IN              msg;

    string          dest_namespace;
    uint64_t        dest_node_id;
    int64_t        callback_id;
    ffresponser_t*  responser;
    string          err_info;
    void response(OUT& out_)
    {
        if (0 != callback_id)
            responser->response(dest_namespace, TYPE_NAME(OUT), dest_node_id, callback_id, ffthrift_t::EncodeAsString(out_));
    }
};

#ifdef FF_ENABLE_PROTOCOLBUF

template<typename IN, typename OUT = null_type_t>
struct ffreq_pb_t
{
    ffreq_pb_t():
        dest_node_id(0),
        callback_id(0),
        responser(NULL)
    {}
    bool error() const { return err_info.empty() == false; }
    const string& error_msg() const { return err_info; }
    IN              msg;

    string          dest_namespace;
    uint64_t        dest_node_id;
    int64_t        callback_id;
    ffresponser_t*  responser;
    string          err_info;
    void response(OUT& out_)
    {
        if (0 != callback_id){
            string ret;
            out_.SerializeToString(&ret);
            responser->response(dest_namespace, TYPE_NAME(OUT), dest_node_id, callback_id, ret);
        }
    }
};

#endif

struct ffrpc_ops_t
{
    //! 接受broker 消息的回调函数
    template <typename R, typename T>
    static ffslot_t::callback_t* gen_callback(R (*)(T&, socket_ptr_t));
    template <typename R, typename CLASS_TYPE, typename T>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*)(T&, socket_ptr_t), CLASS_TYPE* obj_);
    
    //! broker client 注册接口
    template <typename R, typename IN, typename OUT>
    static ffslot_t::callback_t* gen_callback(R (*)(ffreq_t<IN, OUT>&));
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*)(ffreq_t<IN, OUT>&), CLASS_TYPE* obj);

    //! ffrpc call接口的回调函数参数
    //! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1), CLASS_TYPE* obj_, ARG1 arg1_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2, typename FUNC_ARG3, typename ARG3>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2, typename FUNC_ARG3, typename ARG3, typename FUNC_ARG4, typename ARG4>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_, ARG4 arg4_);
    
    //!******************************** pb 使用不同的callback函数 ******************************************

#ifdef FF_ENABLE_PROTOCOLBUF

    template <typename R, typename IN, typename OUT>
    static ffslot_t::callback_t* gen_callback(R (*)(ffreq_pb_t<IN, OUT>&));
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*)(ffreq_pb_t<IN, OUT>&), CLASS_TYPE* obj);

    //! ffrpc call接口的回调函数参数
    //! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1), CLASS_TYPE* obj_, ARG1 arg1_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2, typename FUNC_ARG3, typename ARG3>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_);
    template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1,
              typename FUNC_ARG2, typename ARG2, typename FUNC_ARG3, typename ARG3, typename FUNC_ARG4, typename ARG4>
    static ffslot_t::callback_t* gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_, ARG4 arg4_);
    
#endif
};

template <typename R, typename T>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (*func_)(T&, socket_ptr_t))
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (*func_t)(T&, socket_ptr_t);
        lambda_cb(func_t func_):m_func(func_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_msg_arg))
            {
                return;
            }
            ffslot_msg_arg* msg_data = (ffslot_msg_arg*)args_;
            T msg;
            msg_tool_t::decode(msg, msg_data->body);
            m_func(msg, msg_data->sock);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func); }
        func_t m_func;
    };
    return new lambda_cb(func_);
}
template <typename R, typename CLASS_TYPE, typename T>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(T&, socket_ptr_t), CLASS_TYPE* obj_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(T&, socket_ptr_t);
        lambda_cb(func_t func_, CLASS_TYPE* obj_):m_func(func_), m_obj(obj_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_msg_arg))
            {
                return;
            }
            ffslot_msg_arg* msg_data = (ffslot_msg_arg*)args_;
            T msg;
            msg_tool_t::decode(msg, msg_data->body);
            (m_obj->*(m_func))(msg, msg_data->sock);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
    };
    return new lambda_cb(func_, obj_);
}

//! 注册接口
template <typename R, typename IN, typename OUT>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (*func_)(ffreq_t<IN, OUT>&))
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (*func_t)(ffreq_t<IN, OUT>&);
        lambda_cb(func_t func_):m_func(func_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body, &(req.err_info));
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            m_func(req);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func); }
        func_t m_func;
    };
    return new lambda_cb(func_);
}
template <typename R, typename CLASS_TYPE, typename IN, typename OUT>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&), CLASS_TYPE* obj_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_t<IN, OUT>&);
        lambda_cb(func_t func_, CLASS_TYPE* obj_):m_func(func_), m_obj(obj_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body, &(req.err_info));
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
    };
    return new lambda_cb(func_, obj_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1), CLASS_TYPE* obj_, ARG1 arg1_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_t<IN, OUT>&, FUNC_ARG1);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body, &(req.err_info));
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
    };
    return new lambda_cb(func_, obj_, arg1_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body, &(req.err_info));
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2,
          typename FUNC_ARG3, typename ARG3>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_, const ARG3& arg3_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_), m_arg3(arg3_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body, &(req.err_info));
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2, m_arg3);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2, m_arg3); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
        typename fftraits_t<ARG3>::value_t m_arg3;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_, arg3_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2,
          typename FUNC_ARG3, typename ARG3, typename FUNC_ARG4, typename ARG4>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_, ARG4 arg4_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_, const ARG3& arg3_, const ARG4& arg4_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_), m_arg3(arg3_), m_arg4(arg4_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                msg_tool_t::decode(req.msg, msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2, m_arg3, m_arg4);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2, m_arg3, m_arg4); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
        typename fftraits_t<ARG3>::value_t m_arg3;
        typename fftraits_t<ARG4>::value_t m_arg4;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_, arg3_, arg4_);
}

//!******************************** pb 使用不同的callback函数 ******************************************

#ifdef FF_ENABLE_PROTOCOLBUF

template <typename R, typename IN, typename OUT>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (*func_)(ffreq_pb_t<IN, OUT>&))
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (*func_t)(ffreq_pb_t<IN, OUT>&);
        lambda_cb(func_t func_):m_func(func_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            m_func(req);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func); }
        func_t m_func;
    };
    return new lambda_cb(func_);
}
template <typename R, typename CLASS_TYPE, typename IN, typename OUT>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&), CLASS_TYPE* obj_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_pb_t<IN, OUT>&);
        lambda_cb(func_t func_, CLASS_TYPE* obj_):m_func(func_), m_obj(obj_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
    };
    return new lambda_cb(func_, obj_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1), CLASS_TYPE* obj_, ARG1 arg1_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_){}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
    };
    return new lambda_cb(func_, obj_, arg1_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2,
          typename FUNC_ARG3, typename ARG3>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_, const ARG3& arg3_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_), m_arg3(arg3_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2, m_arg3);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2, m_arg3); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
        typename fftraits_t<ARG3>::value_t m_arg3;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_, arg3_);
}

//! 如果绑定回调函数的时候，有时需要一些临时参数被保存直到回调函数被调用
template <typename R, typename CLASS_TYPE, typename IN, typename OUT, typename FUNC_ARG1, typename ARG1, typename FUNC_ARG2, typename ARG2,
          typename FUNC_ARG3, typename ARG3, typename FUNC_ARG4, typename ARG4>
ffslot_t::callback_t* ffrpc_ops_t::gen_callback(R (CLASS_TYPE::*func_)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4),
                                                CLASS_TYPE* obj_, ARG1 arg1_, ARG2 arg2_, ARG3 arg3_, ARG4 arg4_)
{
    struct lambda_cb: public ffslot_t::callback_t
    {
        typedef R (CLASS_TYPE::*func_t)(ffreq_pb_t<IN, OUT>&, FUNC_ARG1, FUNC_ARG2, FUNC_ARG3, FUNC_ARG4);
        lambda_cb(func_t func_, CLASS_TYPE* obj_, const ARG1& arg1_, const ARG2& arg2_, const ARG3& arg3_, const ARG4& arg4_):
            m_func(func_), m_obj(obj_), m_arg1(arg1_), m_arg2(arg2_), m_arg3(arg3_), m_arg4(arg4_)
        {}
        virtual void exe(ffslot_t::callback_arg_t* args_)
        {
            if (args_->type() != TYPEID(ffslot_req_arg))
            {
                return;
            }
            ffslot_req_arg* msg_data = (ffslot_req_arg*)args_;
            ffreq_pb_t<IN, OUT> req;
            if (msg_data->err_info.empty())
            {
                req.msg.ParseFromString(msg_data->body);
            }
            else
            {
                req.err_info = msg_data->err_info;
            }
            req.dest_node_id = msg_data->dest_node_id;
            req.callback_id = msg_data->callback_id;
            req.dest_namespace = msg_data->dest_namespace;
            req.responser = msg_data->responser;
            (m_obj->*(m_func))(req, m_arg1, m_arg2, m_arg3, m_arg4);
        }
        virtual ffslot_t::callback_t* fork() { return new lambda_cb(m_func, m_obj, m_arg1, m_arg2, m_arg3, m_arg4); }
        func_t      m_func;
        CLASS_TYPE* m_obj;
        typename fftraits_t<ARG1>::value_t m_arg1;
        typename fftraits_t<ARG2>::value_t m_arg2;
        typename fftraits_t<ARG3>::value_t m_arg3;
        typename fftraits_t<ARG4>::value_t m_arg4;
    };
    return new lambda_cb(func_, obj_, arg1_, arg2_, arg3_, arg4_);
}
#endif

enum ffrpc_cmd_def_e
{
    BROKER_TO_CLIENT_MSG = 1,
    //!新版本*********
    REGISTER_TO_BROKER_REQ,
    REGISTER_TO_BROKER_RET,
    BROKER_ROUTE_MSG,
    SYNC_CLIENT_REQ, //! 同步客户端的请求，如python,php
};


//! gate 验证client的sessionid的消息
struct session_verify_t
{
    typedef session_verify_in_t  in_t;
    typedef session_verify_out_t out_t;
};

//!session 第一次进入
struct session_first_entere_t
{
    typedef session_first_entere_in_t  in_t;
    typedef session_first_entere_out_t out_t;
};
//! gate session 进入场景服务器消息
struct session_enter_scene_t
{
    typedef session_enter_scene_in_t  in_t;
    typedef session_enter_scene_out_t out_t;
    
};
//! gate session 下线
struct session_offline_t
{
    typedef session_offline_in_t  in_t;
    typedef session_offline_out_t out_t;
    
};

//! gate 转发client的消息
struct route_logic_msg_t
{
    typedef route_logic_msg_in_t  in_t;
    typedef route_logic_msg_out_t out_t;
};
//! 改变gate 中client 对应的logic节点
struct gate_change_logic_node_t
{
    typedef gate_change_logic_node_in_t  in_t;
    typedef gate_change_logic_node_out_t out_t;
    
};

//! 关闭gate中的某个session
struct gate_close_session_t
{
    typedef gate_close_session_in_t  in_t;
    typedef gate_close_session_out_t out_t;
    
};
//! 转发消息给client
struct gate_route_msg_to_session_t
{
    typedef gate_route_msg_to_session_in_t  in_t;
    typedef gate_route_msg_to_session_out_t out_t;
    
};
//! 转发消息给所有client
struct gate_broadcast_msg_to_session_t
{
    typedef gate_broadcast_msg_to_session_in_t  in_t;
    typedef gate_broadcast_msg_to_session_out_t out_t;
};


//! scene 之间的互调用

struct scene_call_msg_t
{
    typedef scene_call_msg_in_t  in_t;
    typedef scene_call_msg_out_t out_t;
};


//! 用于broker 和 rpc 在内存间投递消息
class ffrpc_t;
class ffbroker_t;
struct ffrpc_memory_route_t
{
    struct dest_node_info_t
    {
        dest_node_info_t(ffrpc_t* rpc_ = NULL, ffbroker_t* broker_ = NULL):
            ffrpc(rpc_),
            ffbroker(broker_)
        {}
        ffrpc_t*        ffrpc;
        ffbroker_t*     ffbroker;
    };

    typedef map<uint64_t/*node id*/, dest_node_info_t> node_info_map_t;
    int add_node(uint64_t node_id_, ffrpc_t* ffrpc_)
    {
        lock_guard_t lock(m_mutex);
        node_info_map_t tmp_data = m_node_info.get_data();
        for (node_info_map_t::iterator it = tmp_data.begin(); it != tmp_data.end(); ++it)
        {
            if (it->second.ffrpc == ffrpc_)
            {
                tmp_data.erase(it);
                break;
            }
        }
        tmp_data[node_id_].ffrpc = ffrpc_;
        m_node_info.update_data(tmp_data);
        return 0;
    }
    int add_node(uint64_t node_id_, ffbroker_t* ffbroker_)
    {
        lock_guard_t lock(m_mutex);
        node_info_map_t tmp_data = m_node_info.get_data();
        for (node_info_map_t::iterator it = tmp_data.begin(); it != tmp_data.end(); ++it)
        {
            if (it->second.ffbroker == ffbroker_)
            {
                tmp_data.erase(it);
                break;
            }
        }
        tmp_data[node_id_].ffbroker = ffbroker_;
        m_node_info.update_data(tmp_data);
        return 0;
    }
    int del_node(uint64_t node_id_)
    {
        lock_guard_t lock(m_mutex);
        node_info_map_t tmp_data = m_node_info.get_data();
        tmp_data.erase(node_id_);
        m_node_info.update_data(tmp_data);
        return 0;
    }
    ffrpc_t* get_rpc(uint64_t node_id_)
    {
        const node_info_map_t& tmp_data = m_node_info.get_data();
        node_info_map_t::const_iterator it = tmp_data.find(node_id_);
        if (it != tmp_data.end())
        {
            return it->second.ffrpc;
        }
        return NULL;
    }
    ffbroker_t* get_broker(uint64_t node_id_)
    {
        const node_info_map_t& tmp_data = m_node_info.get_data();
        node_info_map_t::const_iterator it = tmp_data.find(node_id_);
        if (it != tmp_data.end())
        {
            return it->second.ffbroker;
        }
        return NULL;
    }
    uint64_t get_broker_in_mem()
    {
        const node_info_map_t& tmp_data = m_node_info.get_data();
        node_info_map_t::const_iterator it = tmp_data.begin();
        for (; it != tmp_data.end(); ++it)
        {
            if (NULL != it->second.ffbroker)
            {
                return it->first;
            }
        }
        return 0;
    }
    safe_stl_t<node_info_map_t> m_node_info;
    mutex_t                     m_mutex;
};

//!新版本的实现*********************************************************************************************

//! 处理其他broker或者client注册到此server
struct register_to_broker_t
{
    typedef register_to_broker_in_t  in_t;
    typedef register_to_broker_out_t out_t;
};
//! 处理转发消息的操作
struct broker_route_msg_t
{
    typedef broker_route_msg_in_t in_t;
};


}

#endif
