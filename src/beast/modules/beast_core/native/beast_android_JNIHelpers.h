//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_ANDROID_JNIHELPERS_H_INCLUDED
#define BEAST_ANDROID_JNIHELPERS_H_INCLUDED

#if ! (defined (BEAST_ANDROID_ACTIVITY_CLASSNAME) && defined (BEAST_ANDROID_ACTIVITY_CLASSPATH))
 #error "The BEAST_ANDROID_ACTIVITY_CLASSNAME and BEAST_ANDROID_ACTIVITY_CLASSPATH macros must be set!"
#endif

//==============================================================================
extern JNIEnv* getEnv() noexcept;

//==============================================================================
class GlobalRef
{
public:
    inline GlobalRef() noexcept                 : obj (0) {}
    inline explicit GlobalRef (jobject o)       : obj (retain (o)) {}
    inline GlobalRef (const GlobalRef& other)   : obj (retain (other.obj)) {}
    ~GlobalRef()                                { clear(); }

    inline void clear()
    {
        if (obj != 0)
        {
            getEnv()->DeleteGlobalRef (obj);
            obj = 0;
        }
    }

    inline GlobalRef& operator= (const GlobalRef& other)
    {
        jobject newObj = retain (other.obj);
        clear();
        obj = newObj;
        return *this;
    }

    //==============================================================================
    inline operator jobject() const noexcept    { return obj; }
    inline jobject get() const noexcept         { return obj; }

    //==============================================================================
    #define DECLARE_CALL_TYPE_METHOD(returnType, typeName) \
        returnType call##typeName##Method (jmethodID methodID, ... ) const \
        { \
            va_list args; \
            va_start (args, methodID); \
            returnType result = getEnv()->Call##typeName##MethodV (obj, methodID, args); \
            va_end (args); \
            return result; \
        }

    DECLARE_CALL_TYPE_METHOD (jobject, Object)
    DECLARE_CALL_TYPE_METHOD (jboolean, Boolean)
    DECLARE_CALL_TYPE_METHOD (jbyte, Byte)
    DECLARE_CALL_TYPE_METHOD (jchar, Char)
    DECLARE_CALL_TYPE_METHOD (jshort, Short)
    DECLARE_CALL_TYPE_METHOD (jint, Int)
    DECLARE_CALL_TYPE_METHOD (jlong, Long)
    DECLARE_CALL_TYPE_METHOD (jfloat, Float)
    DECLARE_CALL_TYPE_METHOD (jdouble, Double)
    #undef DECLARE_CALL_TYPE_METHOD

    void callVoidMethod (jmethodID methodID, ... ) const
    {
        va_list args;
        va_start (args, methodID);
        getEnv()->CallVoidMethodV (obj, methodID, args);
        va_end (args);
    }

private:
    //==============================================================================
    jobject obj;

    static inline jobject retain (jobject obj)
    {
        return obj == 0 ? 0 : getEnv()->NewGlobalRef (obj);
    }
};

//==============================================================================
template <typename JavaType>
class LocalRef
{
public:
    explicit inline LocalRef (JavaType o) noexcept      : obj (o) {}
    inline LocalRef (const LocalRef& other) noexcept    : obj (retain (other.obj)) {}
    ~LocalRef()                                         { clear(); }

    void clear()
    {
        if (obj != 0)
            getEnv()->DeleteLocalRef (obj);
    }

    LocalRef& operator= (const LocalRef& other)
    {
        jobject newObj = retain (other.obj);
        clear();
        obj = newObj;
        return *this;
    }

    inline operator JavaType() const noexcept   { return obj; }
    inline JavaType get() const noexcept        { return obj; }

private:
    JavaType obj;

    static JavaType retain (JavaType obj)
    {
        return obj == 0 ? 0 : (JavaType) getEnv()->NewLocalRef (obj);
    }
};

//==============================================================================
namespace
{
    String beastString (JNIEnv* env, jstring s)
    {
        const char* const utf8 = env->GetStringUTFChars (s, nullptr);
        CharPointer_UTF8 utf8CP (utf8);
        const String result (utf8CP);
        env->ReleaseStringUTFChars (s, utf8);
        return result;
    }

    String beastString (jstring s)
    {
        return beastString (getEnv(), s);
    }

    LocalRef<jstring> javaString (const String& s)
    {
        return LocalRef<jstring> (getEnv()->NewStringUTF (s.toUTF8()));
    }

    LocalRef<jstring> javaStringFromChar (const beast_wchar c)
    {
        char utf8[8] = { 0 };
        CharPointer_UTF8 (utf8).write (c);
        return LocalRef<jstring> (getEnv()->NewStringUTF (utf8));
    }
}

//==============================================================================
class JNIClassBase : public Uncopyable
{
public:
    explicit JNIClassBase (const char* classPath);
    virtual ~JNIClassBase();

    inline operator jclass() const noexcept { return classRef; }

    static void initialiseAllClasses (JNIEnv*);
    static void releaseAllClasses (JNIEnv*);

protected:
    virtual void initialiseFields (JNIEnv*) = 0;

    jmethodID resolveMethod (JNIEnv*, const char* methodName, const char* params);
    jmethodID resolveStaticMethod (JNIEnv*, const char* methodName, const char* params);
    jfieldID resolveField (JNIEnv*, const char* fieldName, const char* signature);
    jfieldID resolveStaticField (JNIEnv*, const char* fieldName, const char* signature);

private:
    const char* const classPath;
    jclass classRef;

    static Array<JNIClassBase*>& getClasses();
    void initialise (JNIEnv*);
    void release (JNIEnv*);
};

//==============================================================================
#define CREATE_JNI_METHOD(methodID, stringName, params)         methodID = resolveMethod (env, stringName, params);
#define CREATE_JNI_STATICMETHOD(methodID, stringName, params)   methodID = resolveStaticMethod (env, stringName, params);
#define CREATE_JNI_FIELD(fieldID, stringName, signature)        fieldID  = resolveField (env, stringName, signature);
#define CREATE_JNI_STATICFIELD(fieldID, stringName, signature)  fieldID  = resolveStaticField (env, stringName, signature);
#define DECLARE_JNI_METHOD(methodID, stringName, params)        jmethodID methodID;
#define DECLARE_JNI_FIELD(fieldID, stringName, signature)       jfieldID  fieldID;

#define DECLARE_JNI_CLASS(CppClassName, javaPath) \
    class CppClassName ## _Class   : public JNIClassBase \
    { \
    public: \
        CppClassName ## _Class() : JNIClassBase (javaPath) {} \
    \
        void initialiseFields (JNIEnv* env) \
        { \
            JNI_CLASS_MEMBERS (CREATE_JNI_METHOD, CREATE_JNI_STATICMETHOD, CREATE_JNI_FIELD, CREATE_JNI_STATICFIELD); \
        } \
    \
        JNI_CLASS_MEMBERS (DECLARE_JNI_METHOD, DECLARE_JNI_METHOD, DECLARE_JNI_FIELD, DECLARE_JNI_FIELD); \
    }; \
    static CppClassName ## _Class CppClassName;


//==============================================================================
#define BEAST_JNI_CALLBACK(className, methodName, returnType, params) \
  extern "C" __attribute__ ((visibility("default"))) returnType BEAST_JOIN_MACRO (BEAST_JOIN_MACRO (Java_, className), _ ## methodName) params

//==============================================================================
class AndroidSystem
{
public:
    AndroidSystem();

    void initialise (JNIEnv*, jobject activity, jstring appFile, jstring appDataDir);
    void shutdown (JNIEnv*);

    //==============================================================================
    GlobalRef activity;
    String appFile, appDataDir;
    int screenWidth, screenHeight;
};

extern AndroidSystem android;

//==============================================================================
class ThreadLocalJNIEnvHolder
{
public:
    ThreadLocalJNIEnvHolder()
        : jvm (nullptr)
    {
        zeromem (threads, sizeof (threads));
        zeromem (envs, sizeof (envs));
    }

    void initialise (JNIEnv* env)
    {
        // NB: the DLL can be left loaded by the JVM, so the same static
        // objects can end up being reused by subsequent runs of the app
        zeromem (threads, sizeof (threads));
        zeromem (envs, sizeof (envs));

        env->GetJavaVM (&jvm);
        addEnv (env);
    }

    JNIEnv* attach()
    {
        JNIEnv* env = nullptr;
        jvm->AttachCurrentThread (&env, nullptr);

        if (env != nullptr)
            addEnv (env);

        return env;
    }

    void detach()
    {
        jvm->DetachCurrentThread();

        const pthread_t thisThread = pthread_self();

        SpinLock::ScopedLockType sl (addRemoveLock);
        for (int i = 0; i < maxThreads; ++i)
            if (threads[i] == thisThread)
                threads[i] = 0;
    }

    JNIEnv* getOrAttach() noexcept
    {
        JNIEnv* env = get();

        if (env == nullptr)
            env = attach();

        bassert (env != nullptr);
        return env;
    }

    JNIEnv* get() const noexcept
    {
        const pthread_t thisThread = pthread_self();

        for (int i = 0; i < maxThreads; ++i)
            if (threads[i] == thisThread)
                return envs[i];

        return nullptr;
    }

    enum { maxThreads = 32 };

private:
    JavaVM* jvm;
    pthread_t threads [maxThreads];
    JNIEnv* envs [maxThreads];
    SpinLock addRemoveLock;

    void addEnv (JNIEnv* env)
    {
        SpinLock::ScopedLockType sl (addRemoveLock);

        if (get() == nullptr)
        {
            const pthread_t thisThread = pthread_self();

            for (int i = 0; i < maxThreads; ++i)
            {
                if (threads[i] == 0)
                {
                    envs[i] = env;
                    threads[i] = thisThread;
                    return;
                }
            }
        }

        bassertfalse; // too many threads!
    }
};

extern ThreadLocalJNIEnvHolder threadLocalJNIEnvHolder;

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 METHOD (createNewView,          "createNewView",        "(Z)L" BEAST_ANDROID_ACTIVITY_CLASSPATH "$ComponentPeerView;") \
 METHOD (deleteView,             "deleteView",           "(L" BEAST_ANDROID_ACTIVITY_CLASSPATH "$ComponentPeerView;)V") \
 METHOD (postMessage,            "postMessage",          "(J)V") \
 METHOD (finish,                 "finish",               "()V") \
 METHOD (getClipboardContent,    "getClipboardContent",  "()Ljava/lang/String;") \
 METHOD (setClipboardContent,    "setClipboardContent",  "(Ljava/lang/String;)V") \
 METHOD (excludeClipRegion,      "excludeClipRegion",    "(Landroid/graphics/Canvas;FFFF)V") \
 METHOD (renderGlyph,            "renderGlyph",          "(CLandroid/graphics/Paint;Landroid/graphics/Matrix;Landroid/graphics/Rect;)[I") \
 STATICMETHOD (createHTTPStream, "createHTTPStream",     "(Ljava/lang/String;Z[BLjava/lang/String;ILjava/lang/StringBuffer;)L" BEAST_ANDROID_ACTIVITY_CLASSPATH "$HTTPStream;") \
 METHOD (launchURL,              "launchURL",            "(Ljava/lang/String;)V") \
 METHOD (showMessageBox,         "showMessageBox",       "(Ljava/lang/String;Ljava/lang/String;J)V") \
 METHOD (showOkCancelBox,        "showOkCancelBox",      "(Ljava/lang/String;Ljava/lang/String;J)V") \
 METHOD (showYesNoCancelBox,     "showYesNoCancelBox",   "(Ljava/lang/String;Ljava/lang/String;J)V") \
 STATICMETHOD (getLocaleValue,   "getLocaleValue",       "(Z)Ljava/lang/String;") \
 METHOD (scanFile,               "scanFile",             "(Ljava/lang/String;)V")

DECLARE_JNI_CLASS (BeastAppActivity, BEAST_ANDROID_ACTIVITY_CLASSPATH);
#undef JNI_CLASS_MEMBERS

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 METHOD (constructor,   "<init>",           "(I)V") \
 METHOD (setColor,      "setColor",         "(I)V") \
 METHOD (setAlpha,      "setAlpha",         "(I)V") \
 METHOD (setTypeface,   "setTypeface",      "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;") \
 METHOD (ascent,        "ascent",           "()F") \
 METHOD (descent,       "descent",          "()F") \
 METHOD (setTextSize,   "setTextSize",      "(F)V") \
 METHOD (getTextWidths, "getTextWidths",    "(Ljava/lang/String;[F)I") \
 METHOD (setTextScaleX, "setTextScaleX",    "(F)V") \
 METHOD (getTextPath,   "getTextPath",      "(Ljava/lang/String;IIFFLandroid/graphics/Path;)V") \
 METHOD (setShader,     "setShader",        "(Landroid/graphics/Shader;)Landroid/graphics/Shader;") \

DECLARE_JNI_CLASS (Paint, "android/graphics/Paint");
#undef JNI_CLASS_MEMBERS

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 METHOD (constructor,   "<init>",    "()V") \
 METHOD (setValues,     "setValues", "([F)V") \

DECLARE_JNI_CLASS (Matrix, "android/graphics/Matrix");
#undef JNI_CLASS_MEMBERS

//==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD) \
 METHOD (constructor,   "<init>",   "(IIII)V") \
 FIELD (left,           "left",     "I") \
 FIELD (right,          "right",    "I") \
 FIELD (top,            "top",      "I") \
 FIELD (bottom,         "bottom",   "I") \

DECLARE_JNI_CLASS (RectClass, "android/graphics/Rect");
#undef JNI_CLASS_MEMBERS

#endif   // BEAST_ANDROID_JNIHELPERS_H_INCLUDED
