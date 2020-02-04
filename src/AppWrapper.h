#include "App.h"
#include <v8.h>
#include "Utilities.h"
using namespace v8;

/* uWS.App.ws('/pattern', behavior) */
template <typename APP>
void uWS_App_ws(const FunctionCallbackInfo<Value> &args) {

    Isolate *isolate = args.GetIsolate();

    PerContextData *perContextData = (PerContextData *) Local<External>::Cast(args.Data())->Value();

    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);
    /* This one is default constructed with defaults */
    typename APP::WebSocketBehavior behavior = {};

    NativeString pattern(args.GetIsolate(), args[0]);
    if (pattern.isInvalid(args)) {
        return;
    }

    UniquePersistent<Function> openPf;
    UniquePersistent<Function> messagePf;
    UniquePersistent<Function> drainPf;
    UniquePersistent<Function> closePf;

    struct PerSocketData {
        UniquePersistent<Object> *socketPf;
    };

    /* Get the behavior object */
    if (args.Length() == 2) {
        Local<Object> behaviorObject = Local<Object>::Cast(args[1]);

        /* maxPayloadLength or default */
        MaybeLocal<Value> maybeMaxPayloadLength = behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "maxPayloadLength", NewStringType::kNormal).ToLocalChecked());
        if (!maybeMaxPayloadLength.IsEmpty() && !maybeMaxPayloadLength.ToLocalChecked()->IsUndefined()) {
            behavior.maxPayloadLength = maybeMaxPayloadLength.ToLocalChecked()->Int32Value(isolate->GetCurrentContext()).ToChecked();
        }

        /* idleTimeout or default */
        MaybeLocal<Value> maybeIdleTimeout = behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "idleTimeout", NewStringType::kNormal).ToLocalChecked());
        if (!maybeIdleTimeout.IsEmpty() && !maybeIdleTimeout.ToLocalChecked()->IsUndefined()) {
            behavior.idleTimeout = maybeIdleTimeout.ToLocalChecked()->Int32Value(isolate->GetCurrentContext()).ToChecked();
        }

        /* Compression or default, map from 0, 1, 2 to disabled, shared, dedicated. This is actually the enum */
        MaybeLocal<Value> maybeCompression = behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "compression", NewStringType::kNormal).ToLocalChecked());
        if (!maybeCompression.IsEmpty() && !maybeCompression.ToLocalChecked()->IsUndefined()) {
            behavior.compression = (uWS::CompressOptions) maybeCompression.ToLocalChecked()->Int32Value(isolate->GetCurrentContext()).ToChecked();
        }

        /* maxBackpressure or default */
        MaybeLocal<Value> maybeMaxBackpressure = behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "maxBackpressure", NewStringType::kNormal).ToLocalChecked());
        if (!maybeMaxBackpressure.IsEmpty() && !maybeMaxBackpressure.ToLocalChecked()->IsUndefined()) {
            behavior.maxBackpressure = maybeMaxBackpressure.ToLocalChecked()->Int32Value(isolate->GetCurrentContext()).ToChecked();
        }

        /* Open */
        openPf.Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "open", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked()));
        /* Message */
        messagePf.Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "message", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked()));
        /* Drain */
        drainPf.Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "drain", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked()));
        /* Close */
        closePf.Reset(args.GetIsolate(), Local<Function>::Cast(behaviorObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "close", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked()));
    }

    /* Open handler is NOT optional for the wrapper */
    behavior.open = [openPf = std::move(openPf), perContextData](auto *ws, auto *req) {
        Isolate *isolate = perContextData->isolate;
        HandleScope hs(isolate);

        /* Create a new websocket object */
        Local<Object> wsObject = perContextData->wsTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
        wsObject->SetAlignedPointerInInternalField(0, ws);

        /* Create the HttpRequest wrapper */
        Local<Object> reqObject = perContextData->reqTemplate.Get(isolate)->Clone();
        reqObject->SetAlignedPointerInInternalField(0, req);

        /* Attach a new V8 object with pointer to us, to us */
        PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
        perSocketData->socketPf = new UniquePersistent<Object>;
        perSocketData->socketPf->Reset(isolate, wsObject);

        Local<Function> openLf = Local<Function>::New(isolate, openPf);
        if (!openLf->IsUndefined()) {
            Local<Value> argv[] = {wsObject, reqObject};
            CallJS(isolate, openLf, 2, argv);
        }
    };

    /* Message handler is always optional */
    if (messagePf != Undefined(isolate)) {
        behavior.message = [messagePf = std::move(messagePf), isolate](auto *ws, std::string_view message, uWS::OpCode opCode) {
            HandleScope hs(isolate);

            Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer::New(isolate, (void *) message.data(), message.length());

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            Local<Value> argv[3] = {Local<Object>::New(isolate, *(perSocketData->socketPf)),
                                    messageArrayBuffer,
                                    Boolean::New(isolate, opCode == uWS::OpCode::BINARY)};

            CallJS(isolate, Local<Function>::New(isolate, messagePf), 3, argv);

            /* Important: we clear the ArrayBuffer to make sure it is not invalidly used after return */
            messageArrayBuffer->Neuter();
        };
    }

    /* Drain handler is always optional */
    if (drainPf != Undefined(isolate)) {
        behavior.drain = [drainPf = std::move(drainPf), isolate](auto *ws) {
            HandleScope hs(isolate);

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            Local<Value> argv[1] = {Local<Object>::New(isolate, *(perSocketData->socketPf))
                                    };
            CallJS(isolate, Local<Function>::New(isolate, drainPf), 1, argv);
        };
    }

    /* These are not hooked in */
    behavior.ping = [](auto *ws) {

    };

    behavior.pong = [](auto *ws) {

    };

    /* Close handler is NOT optional for the wrapper */
    behavior.close = [closePf = std::move(closePf), isolate](auto *ws, int code, std::string_view message) {
        HandleScope hs(isolate);

        Local<ArrayBuffer> messageArrayBuffer = ArrayBuffer::New(isolate, (void *) message.data(), message.length());
        PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
        Local<Object> wsObject = Local<Object>::New(isolate, *(perSocketData->socketPf));

        /* Invalidate this wsObject */
        wsObject->SetAlignedPointerInInternalField(0, nullptr);

        /* Only call close handler if we have one set */
        Local<Function> closeLf = Local<Function>::New(isolate, closePf);
        if (!closeLf->IsUndefined()) {
            Local<Value> argv[3] = {wsObject, Integer::New(isolate, code), messageArrayBuffer};
            CallJS(isolate, closeLf, 3, argv);
        }

        delete perSocketData->socketPf;

        /* Again, here we clear the buffer to avoid strange bugs */
        messageArrayBuffer->Neuter();
    };

    app->template ws<PerSocketData>(std::string(pattern.getString()), std::move(behavior));

    /* Return this */
    args.GetReturnValue().Set(args.Holder());
}

/* This method wraps get, post and all http methods */
template <typename APP, typename F>
void uWS_App_get(F f, const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    NativeString pattern(args.GetIsolate(), args[0]);
    if (pattern.isInvalid(args)) {
        return;
    }

    /* This function requires perContextData */
    PerContextData *perContextData = (PerContextData *) Local<External>::Cast(args.Data())->Value();
    UniquePersistent<Function> cb(args.GetIsolate(), Local<Function>::Cast(args[1]));

    (app->*f)(std::string(pattern.getString()), [cb = std::move(cb), perContextData](auto *res, auto *req) {
        Isolate *isolate = perContextData->isolate;
        HandleScope hs(isolate);

        Local<Object> resObject = perContextData->resTemplate[getAppTypeIndex<APP>()].Get(isolate)->Clone();
        resObject->SetAlignedPointerInInternalField(0, res);

        Local<Object> reqObject = perContextData->reqTemplate.Get(isolate)->Clone();
        reqObject->SetAlignedPointerInInternalField(0, req);

        Local<Value> argv[] = {resObject, reqObject};
        CallJS(isolate, cb.Get(isolate), 2, argv);

        /* Properly invalidate req */
        reqObject->SetAlignedPointerInInternalField(0, nullptr);

        /* µWS itself will terminate if not responded and not attached
         * onAborted handler, so we can assume it's done */
    });

    args.GetReturnValue().Set(args.Holder());
}

template <typename APP>
void uWS_App_listen(const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    Isolate *isolate = args.GetIsolate();

    /* Require at least two arguments */
    if (args.Length() < 2) {
        /* Throw here */
        args.GetReturnValue().Set(isolate->ThrowException(String::NewFromUtf8(isolate, "App.listen requires port and callback", NewStringType::kNormal).ToLocalChecked()));
        return;
    }

    /* Callback is last */
    auto cb = [&args, isolate](auto *token) {
        /* Return a false boolean if listen failed */
        Local<Value> argv[] = {token ? Local<Value>::Cast(External::New(isolate, token)) : Local<Value>::Cast(Boolean::New(isolate, false))};
        CallJS(isolate, Local<Function>::Cast(args[args.Length() - 1]), 1, argv);
    };

    /* Host is first, if present */
    std::string host;
    if (!args[0]->IsNumber()) {
        NativeString h(isolate, args[0]);
        if (h.isInvalid(args)) {
            return;
        }
        host = h.getString();
    }

    /* Port, options are in the middle, if present */
    std::vector<int> numbers;
    for (int i = std::min<int>(1, host.length()); i < args.Length() - 1; i++) {
        numbers.push_back(args[i]->Uint32Value(args.GetIsolate()->GetCurrentContext()).ToChecked());
    }

    /* We only use the most complete overload */
    app->listen(host, numbers.size() ? numbers[0] : 0,
                numbers.size() > 1 ? numbers[1] : 0, std::move(cb));

    args.GetReturnValue().Set(args.Holder());
}

template <typename APP>
void uWS_App_publish(const FunctionCallbackInfo<Value> &args) {
    APP *app = (APP *) args.Holder()->GetAlignedPointerFromInternalField(0);

    Isolate *isolate = args.GetIsolate();

    NativeString topic(isolate, args[0]);
    NativeString message(isolate, args[1]);
    app->publish(topic.getString(), message.getString(), BooleanValue(isolate, args[2]) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT, BooleanValue(isolate, args[3]));
}

template <typename APP>
void uWS_App(const FunctionCallbackInfo<Value> &args) {

    Isolate *isolate = args.GetIsolate();
    Local<FunctionTemplate> appTemplate = FunctionTemplate::New(isolate);
    appTemplate->SetClassName(String::NewFromUtf8(isolate, std::is_same<APP, uWS::SSLApp>::value ? "uWS.SSLApp" : "uWS.App", NewStringType::kNormal).ToLocalChecked());

    /* Read the options object if any */
    us_socket_context_options_t options = {};
    std::string keyFileName, certFileName, passphrase, dhParamsFileName, caFileName;
    if (args.Length() == 1) {

        Local<Object> optionsObject = Local<Object>::Cast(args[0]);

        /* Key file name */
        NativeString keyFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "key_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
        if (keyFileNameValue.isInvalid(args)) {
            return;
        }
        if (keyFileNameValue.getString().length()) {
            keyFileName = keyFileNameValue.getString();
            options.key_file_name = keyFileName.c_str();
        }

        /* Cert file name */
        NativeString certFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "cert_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
        if (certFileNameValue.isInvalid(args)) {
            return;
        }
        if (certFileNameValue.getString().length()) {
            certFileName = certFileNameValue.getString();
            options.cert_file_name = certFileName.c_str();
        }

        /* Passphrase */
        NativeString passphraseValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "passphrase", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
        if (passphraseValue.isInvalid(args)) {
            return;
        }
        if (passphraseValue.getString().length()) {
            passphrase = passphraseValue.getString();
            options.passphrase = passphrase.c_str();
        }

        /* DH params file name */
        NativeString dhParamsFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "dh_params_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
        if (dhParamsFileNameValue.isInvalid(args)) {
            return;
        }
        if (dhParamsFileNameValue.getString().length()) {
            dhParamsFileName = dhParamsFileNameValue.getString();
            options.dh_params_file_name = dhParamsFileName.c_str();
        }

        /* CA file name */
        NativeString caFileNameValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "ca_file_name", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
        if (caFileNameValue.isInvalid(args)) {
            return;
        }
        if (caFileNameValue.getString().length()) {
            caFileName = caFileNameValue.getString();
            options.ca_file_name = caFileName.c_str();
        }

        /* ssl_prefer_low_memory_usage */
        options.ssl_prefer_low_memory_usage = BooleanValue(isolate, optionsObject->Get(isolate->GetCurrentContext(), String::NewFromUtf8(isolate, "ssl_prefer_low_memory_usage", NewStringType::kNormal).ToLocalChecked()).ToLocalChecked());
    }

    /* uSockets copies strings here */
    APP *app = new APP(options);

    /* Throw if we failed to construct the app */
    if (app->constructorFailed()) {
        delete app;
        args.GetReturnValue().Set(isolate->ThrowException(String::NewFromUtf8(isolate, "App construction failed", NewStringType::kNormal).ToLocalChecked()));
        return;
    }

    appTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    /* All the http methods */
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "get", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::get, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "post", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::post, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "options", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::options, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "del", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::del, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "patch", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::patch, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "put", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::put, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "head", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::head, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "connect", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::connect, args);
    }, args.Data()));

    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "trace", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::trace, args);
    }, args.Data()));

    /* Any http method */
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "any", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, [](auto &args) {
        uWS_App_get<APP>(&APP::any, args);
    }, args.Data()));

    /* ws, listen */
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "ws", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, uWS_App_ws<APP>, args.Data()));
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "listen", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, uWS_App_listen<APP>, args.Data()));
    appTemplate->PrototypeTemplate()->Set(String::NewFromUtf8(isolate, "publish", NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, uWS_App_publish<APP>, args.Data()));

    Local<Object> localApp = appTemplate->GetFunction(isolate->GetCurrentContext()).ToLocalChecked()->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
    localApp->SetAlignedPointerInInternalField(0, app);

    PerContextData *perContextData = (PerContextData *) Local<External>::Cast(args.Data())->Value();

    /* Add this to our delete list */
    if constexpr (std::is_same<APP, uWS::SSLApp>::value) {
        perContextData->sslApps.emplace_back(app);
    } else {
        perContextData->apps.emplace_back(app);
    }

    args.GetReturnValue().Set(localApp);
}
