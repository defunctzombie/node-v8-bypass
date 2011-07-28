#include <map>
#include <vector>
#include <string>

#include <boost/shared_ptr.hpp>

#include <v8.h>
#include <node.h>

using namespace v8;
using namespace boost;
using namespace node;

namespace {
class JsValue
{
public:
    /// process the local value into a JsValue object
    //virtual shared_ptr<JsValue> from_v8(const Local<Value> val) = 0;

    /// return a v8 value to be passed back to the VM
    virtual Handle<Value> to_v8() const = 0;

    virtual ~JsValue() {}
};

/// main processing method for turning a v8 object into cache
shared_ptr<JsValue> from_v8(const Handle<Value> val);

/// when the type is not supported
class JsUndefined : public JsValue
{
public:
    virtual Handle<Value> to_v8() const
    {
        return Undefined();
    }
};

class JsString : public JsValue
{
    char* m_buff;
    int m_size;

public:
    static shared_ptr<JsValue> from_v8(const Local<String> val)
    {
        shared_ptr<JsString> s(new JsString());

        s->m_size = val->Utf8Length();
        s->m_buff = new char[s->m_size];
        val->WriteUtf8(s->m_buff, s->m_size);

        return s;
    }

    virtual Handle<Value> to_v8() const
    {
        return String::New(m_buff, m_size);
    }

    JsString()
        : m_buff(0)
    {}

    ~JsString()
    {
        if (m_buff)
            delete[] m_buff;
    }
};

/// v8::Number
class JsNumber : public JsValue
{
    double m_val;

public:
    static shared_ptr<JsValue> from_v8(const Local<Number> val)
    {
        return shared_ptr<JsNumber>(new JsNumber(val->NumberValue()));
    }

    virtual Handle<Value> to_v8() const
    {
        return Number::New(m_val);
    }

    // don't use directly, only use from_v8 instead
    // this is here for the shared pointer
    JsNumber(double val)
        : m_val(val)
    {}

};

/// v8::Int32
class JsInt32 : public JsValue
{
    int32_t m_val;

public:
    static shared_ptr<JsValue> from_v8(const Local<Number> val)
    {
        return shared_ptr<JsInt32>(new JsInt32(val->Int32Value()));
    }

    virtual Handle<Value> to_v8() const
    {
        return Int32::New(m_val);
    }

    JsInt32(int32_t val)
        : m_val(val)
    {}
};

/// v8::Uint32
class JsUint32 : public JsValue
{
    uint32_t m_val;

public:
    static shared_ptr<JsValue> from_v8(const Local<Number> val)
    {
        return shared_ptr<JsUint32>(new JsUint32(val->Uint32Value()));
    }

    virtual Handle<Value> to_v8() const
    {
        return Uint32::New(m_val);
    }

    JsUint32(uint32_t val)
        : m_val(val)
    {}
};

/// v8::Array
class JsArray : public JsValue
{
    // store other JsValue objects
    std::vector<shared_ptr<JsValue> > m_vals;

public:

    static shared_ptr<JsValue> from_v8(const Local<Object> val)
    {
        shared_ptr<JsArray> out(new JsArray());

        Local<Array> arr = Local<Array>::Cast(val);

        uint32_t length = arr->Length();
        for (uint32_t i=0 ; i<length ; ++i)
        {
            Local<Value> v = arr->Get(i);
            out->m_vals.push_back(::from_v8(v));
        }

        return out;
    }

    virtual Handle<Value> to_v8() const
    {
        // result object
        const size_t size = m_vals.size();
        Local<Array> out = Array::New(size);

        for (size_t i=0 ; i<size ; ++i)
        {
            const shared_ptr<JsValue>& val = m_vals[i];
            if (val)
                out->Set(i, val->to_v8());
        }

        return Handle<Value>(out);
    }
};

/// v8::Object
class JsObj : public JsValue
{
    // values of the javascript object
    typedef std::map<std::string, shared_ptr<JsValue> > MemberMap;
    MemberMap m_values;

public:

    static shared_ptr<JsValue> from_v8(const Local<Object> val)
    {
        shared_ptr<JsObj> out(new JsObj());

        Local<Object> o = val;
        Local<Array> a = o->GetPropertyNames();

        const uint32_t length = a->Length();
        for (uint32_t i=0 ; i<length ; ++i)
        {
            Local<Value> k = a->Get(i);

            String::AsciiValue key(k);
            std::string s(key.operator *(), key.length());

            // need to use the global from_v8 here
            out->m_values[s] = ::from_v8(o->Get(k));
        }

        return out;
    }

    // convert yourself into a v8 object
    Handle<Value> to_v8() const
    {
        // result object
        Local<Object> obj = Object::New();

        MemberMap::const_iterator iter = m_values.begin();

        for (; iter != m_values.end() ; ++iter)
        {
            Local<String> key = String::New(iter->first.c_str());

            const shared_ptr<JsValue> val = iter->second;
            if (val)
                obj->Set(key, val->to_v8());
        }

        return Handle<Value>(obj);
    }
};

/// process the v8 value and return it wrapped in a JsValue object
/// generic callout
shared_ptr<JsValue> from_v8(const Handle<Value> v8obj)
{
    if (v8obj->IsNumber())
    {
        if (v8obj->IsInt32())
        {
            return JsInt32::from_v8(v8obj->ToNumber());
        }
        else
        {
            return JsNumber::from_v8(v8obj->ToNumber());
        }
    }
    else if (v8obj->IsString())
    {
        return JsString::from_v8(v8obj->ToString());
    }
    else if (v8obj->IsArray())
    {
        return JsArray::from_v8(v8obj->ToObject());
    }
    else if (v8obj->IsObject())
    {
        return JsObj::from_v8(v8obj->ToObject());
    }

    return shared_ptr<JsUndefined>(new JsUndefined());
}

class BypassStore : ObjectWrap
{
    typedef std::map<int64_t, shared_ptr<JsValue> > CacheMap;
    CacheMap m_cache;

public:
    static void Init(Handle<Object> target)
    {
        static Persistent<FunctionTemplate> ft;

        HandleScope scope;

        Local<FunctionTemplate> t = FunctionTemplate::New(New);

        ft = Persistent<FunctionTemplate>::New(t);
        ft->InstanceTemplate()->SetInternalFieldCount(1);
        ft->SetClassName(String::NewSymbol("BypassStore"));

        NODE_SET_PROTOTYPE_METHOD(ft, "set", Set);
        NODE_SET_PROTOTYPE_METHOD(ft, "get", Get);
        NODE_SET_PROTOTYPE_METHOD(ft, "del", Del);
        NODE_SET_PROTOTYPE_METHOD(ft, "list", List);

        target->Set(String::NewSymbol("BypassStore"), ft->GetFunction());
    }

private:
    static Handle<Value> New(const Arguments& args)
    {
        HandleScope scope;
        BypassStore* store = new BypassStore();
        store->Wrap(args.This());
        return args.This();
    }

    /// load data infor your buffer
    static Handle<Value> Set(const Arguments& args)
    {
        HandleScope scope;

        const Handle<Value> key = args[0];
        const Handle<Value> val = args[1];

        const int64_t k = key->IntegerValue();

        BypassStore* store = ObjectWrap::Unwrap<BypassStore>(args.This());
        store->m_cache[k] = from_v8(val);

        return scope.Close(Handle<Value>());
    }

    static Handle<Value> Get(const Arguments& args)
    {
        HandleScope scope;

        const Handle<Value> key = args[0];

        const int64_t k = key->IntegerValue();

        BypassStore* store = ObjectWrap::Unwrap<BypassStore>(args.This());
        CacheMap::const_iterator iter = store->m_cache.find(k);

        if (iter == store->m_cache.end())
            return Undefined();

        return scope.Close(iter->second->to_v8());
    }

    static Handle<Value> Del(const Arguments& args)
    {
        HandleScope scope;

        const Handle<Value> key = args[0];
        const int64_t k = key->IntegerValue();

        BypassStore* store = ObjectWrap::Unwrap<BypassStore>(args.This());
        store->m_cache.erase(k);

        return scope.Close(Handle<Value>());
    }

    static Handle<Value> List(const Arguments& args)
    {
        HandleScope scope;

        Local<Array> arr = Array::New();

        BypassStore* store = ObjectWrap::Unwrap<BypassStore>(args.This());
        CacheMap::const_iterator iter = store->m_cache.begin();
        for (uint32_t i=0; iter != store->m_cache.end() ; ++iter, ++i)
        {
            arr->Set(i, Int32::New(iter->first));
        }

        return scope.Close(arr);
    }
};
}

extern "C" void
init (Handle<Object> target)
{
    BypassStore::Init(target);
}
