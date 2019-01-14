// App.post and all other methods - requires good templating?
// test so that we pass Autobahn with compression/without compression with SSL/without SSL

/* Helping QtCreator */
#include <v8.h>
#include "Utilities.h"
using namespace v8;

/* uWS.App.ws('/pattern', options) */
template <typename APP>
void uWS_App_ws(const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    // pattern
    NativeString nativeString(args.GetIsolate(), args[0]);

    // todo: small leak here, should be unique_ptrs moved in
    Persistent<Function> *openPf = new Persistent<Function>();
    Persistent<Function> *messagePf = new Persistent<Function>();
    Persistent<Function> *drainPf = new Persistent<Function>();
    Persistent<Function> *closePf = new Persistent<Function>();

    int maxPayloadLength = 0;

    /* For now, let's have 0, 1, 2 be from nothing to shared, to dedicated */
    int compression = 0;
    uWS::CompressOptions mappedCompression = uWS::CompressOptions::DISABLED;

    struct PerSocketData {
        Persistent<Object> *socketPf;
    };

    /* Get the behavior object */
    if (args.Length() == 2) {
        Local<Object> behaviorObject = Local<Object>::Cast(args[1]);

        /* maxPayloadLength */
        maxPayloadLength = behaviorObject->Get(String::NewFromUtf8(isolate, "maxPayloadLength"))->Int32Value();

        /* Compression */
        compression = behaviorObject->Get(String::NewFromUtf8(isolate, "compression"))->Int32Value();

        /* Open */
        openPf->Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(String::NewFromUtf8(isolate, "open"))));
        /* Message */
        messagePf->Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(String::NewFromUtf8(isolate, "message"))));
        /* Drain */
        drainPf->Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(String::NewFromUtf8(isolate, "drain"))));
        /* Close */
        closePf->Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(String::NewFromUtf8(isolate, "close"))));
    }

    /* Map compression options from integer values */
    if (compression == 1) {
        mappedCompression = uWS::CompressOptions::SHARED_COMPRESSOR;
    } else if (compression == 2) {
        mappedCompression = uWS::CompressOptions::DEDICATED_COMPRESSOR;
    }

    app->template ws<PerSocketData>(std::string(nativeString.getData(), nativeString.getLength()), {
        /* idleTimeout */
        .compression = mappedCompression,
        .maxPayloadLength = maxPayloadLength,
        /* Handlers */
        .open = [openPf](auto *ws, auto *req) {
            HandleScope hs(isolate);

            /* Create a new websocket object */
            Local<Object> wsObject = WebSocketWrapper::getWsInstance<APP>();
            wsObject->SetAlignedPointerInInternalField(0, ws);

            /* Attach a new V8 object with pointer to us, to us */
            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            perSocketData->socketPf = new Persistent<Object>;
            perSocketData->socketPf->Reset(isolate, wsObject);

            Local<Value> argv[] = {wsObject};
            Local<Function>::New(isolate, *openPf)->Call(isolate->GetCurrentContext()->Global(), 1, argv);
        },
        .message = [messagePf](auto *ws, std::string_view message, uWS::OpCode opCode) {
            HandleScope hs(isolate);

            Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer::New(isolate, (void *) message.data(), message.length());

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            Local<Value> argv[3] = {Local<Object>::New(isolate, *(perSocketData->socketPf)),
                                    /*ArrayBuffer::New(isolate, (void *) message.data(), message.length())*/ messageArrayBuffer,
                                    Boolean::New(isolate, opCode == uWS::OpCode::BINARY)
                                   };
            Local<Function>::New(isolate, *messagePf)->Call(isolate->GetCurrentContext()->Global(), 3, argv);

            /* Important: we clear the ArrayBuffer to make sure it is not invalidly used after return */
            messageArrayBuffer->Neuter();
        },
        .drain = [drainPf](auto *ws) {
            HandleScope hs(isolate);

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            Local<Value> argv[1] = {Local<Object>::New(isolate, *(perSocketData->socketPf))
                                   };
            Local<Function>::New(isolate, *drainPf)->Call(isolate->GetCurrentContext()->Global(), 1, argv);
        },
        .ping = [](auto *ws) {

        },
        .pong = [](auto *ws) {

        },
        .close = [closePf](auto *ws, int code, std::string_view message) {
            HandleScope hs(isolate);

            Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer::New(isolate, (void *) message.data(), message.length());

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            Local<Value> argv[3] = {Local<Object>::New(isolate, *(perSocketData->socketPf)),
                                    Integer::New(isolate, code),
                                    messageArrayBuffer
                                   };
            Local<Function>::New(isolate, *closePf)->Call(isolate->GetCurrentContext()->Global(), 3, argv);

            /* Again, here we clear the buffer to avoid strange bugs */
            messageArrayBuffer->Neuter();
        }
    });

    /* Return this */
    args.GetReturnValue().Set(args.Holder());
}

// todo: all other methods, in particular post!
template <typename APP>
void uWS_App_get(const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    NativeString nativeString(args.GetIsolate(), args[0]);

    Persistent<Function> *pf = new Persistent<Function>();
    pf->Reset(args.GetIsolate(), Local<Function>::Cast(args[1]));

    //Persistent<Function, CopyablePersistentTraits<Function>> p(isolate, Local<Function>::Cast(args[1]));

    app->get(std::string(nativeString.getData(), nativeString.getLength()), [pf](auto *res, auto *req) {
        HandleScope hs(isolate);

        Local<Object> resObject = HttpResponseWrapper::getResInstance<APP>();
        resObject->SetAlignedPointerInInternalField(0, res);

        Local<Object> reqObject = HttpRequestWrapper::getReqInstance();
        reqObject->SetAlignedPointerInInternalField(0, req);

        Local<Value> argv[] = {resObject, reqObject};
        Local<Function>::New(isolate, *pf)->Call(isolate->GetCurrentContext()->Global(), 2, argv);
    });

    args.GetReturnValue().Set(args.Holder());
}

template <typename APP>
void uWS_App_listen(const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    int port = args[0]->Uint32Value(args.GetIsolate()->GetCurrentContext()).ToChecked();

    app->listen(port, [&args](auto *token) {
        Local<Value> argv[] = {Boolean::New(isolate, token)};
        Local<Function>::Cast(args[1])->Call(isolate->GetCurrentContext()->Global(), 1, argv);
    });

    // Return this
    args.GetReturnValue().Set(args.Holder());
}

template <typename APP>
void uWS_App(const FunctionCallbackInfo<Value> &args) {
    Local<FunctionTemplate> appTemplate = FunctionTemplate::New(isolate);

    APP *app;

    /* Name differs based on type */
    if (std::is_same<APP, uWS::SSLApp>::value) {
        appTemplate->SetClassName(String::NewFromUtf8(isolate, "uWS.SSLApp"));

        /* We fill these below */
        us_ssl_socket_context_options ssl_options = {};

        static std::string keyFileName;
        static std::string certFileName;
        static std::string passphrase;

        /* Read the options object (SSL options) */
        if (args.Length() == 1) {
            /* Key file name */
            NativeString keyFileNameValue(isolate, Local<Object>::Cast(args[0])->Get(String::NewFromUtf8(isolate, "key_file_name")));
            if (keyFileNameValue.getLength()) {
                keyFileName.append(keyFileNameValue.getData(), keyFileNameValue.getLength());
                ssl_options.key_file_name = keyFileName.c_str();
            }

            /* Cert file name */
            NativeString certFileNameValue(isolate, Local<Object>::Cast(args[0])->Get(String::NewFromUtf8(isolate, "cert_file_name")));
            if (certFileNameValue.getLength()) {
                certFileName.append(certFileNameValue.getData(), certFileNameValue.getLength());
                ssl_options.cert_file_name = certFileName.c_str();
            }

            /* Passphrase */
            NativeString passphraseValue(isolate, Local<Object>::Cast(args[0])->Get(String::NewFromUtf8(isolate, "passphrase")));
            if (passphraseValue.getLength()) {
                passphrase.append(passphraseValue.getData(), passphraseValue.getLength());
                ssl_options.passphrase = passphrase.c_str();
            }
        }

        app = new APP(ssl_options);
    } else {
        appTemplate->SetClassName(String::NewFromUtf8(isolate, "uWS.App"));
        app = new APP;
    }

    appTemplate->InstanceTemplate()->SetInternalFieldCount(1);


    /* Most used functions will be get, post, ws, listen */

    // Get and all the Http methods
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "get"), FunctionTemplate::New(isolate, uWS_App_get<APP>));

    // Ws
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "ws"), FunctionTemplate::New(isolate, uWS_App_ws<APP>));

    // Listen
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "listen"), FunctionTemplate::New(isolate, uWS_App_listen<APP>));

    // Instantiate and set intenal pointer
    Local<Object> localApp = appTemplate->GetFunction()->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();

    // Delete this boy
    localApp->SetAlignedPointerInInternalField(0, app);

    // Return an instance of this shit
    args.GetReturnValue().Set(localApp);
}
