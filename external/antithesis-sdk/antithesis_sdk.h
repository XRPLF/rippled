#pragma once

// This header file contains the Antithesis C++ SDK, which enables C++ applications to integrate with the [Antithesis platform].
//
// Documentation for the SDK is found at https://antithesis.com/docs/using_antithesis/sdk/cpp/overview.html.

#include <cstdint>
#include <random>
#include <string>
#include <map>
#include <set>
#include <variant>
#include <vector>
#include <utility>

namespace antithesis {
    inline const char* SDK_VERSION = "0.4.0";
    inline const char* PROTOCOL_VERSION = "1.1.0";

    struct LocalRandom {
        std::random_device device;
        std::mt19937_64 gen;
        std::uniform_int_distribution<unsigned long long> distribution;

        LocalRandom() : device(), gen(device()), distribution() {}

        uint64_t random() {
#ifdef ANTITHESIS_RANDOM_OVERRIDE
            return ANTITHESIS_RANDOM_OVERRIDE();
#else
            return distribution(gen);
#endif
        }
    };

    struct JSON; struct JSONArray;
    typedef std::variant<JSON, std::nullptr_t, std::string, bool, char, int, uint64_t, float, double, const char*, JSONArray> JSONValue;

    struct JSONArray : std::vector<JSONValue> {
        using std::vector<JSONValue>::vector;

        template<typename T, std::enable_if<std::is_convertible<T, JSONValue>::value, bool>::type = true>
        JSONArray(std::vector<T> vals) : std::vector<JSONValue>(vals.begin(), vals.end()) {}
    };

    struct JSON : std::map<std::string, JSONValue> {
        JSON() : std::map<std::string, JSONValue>() {}
        JSON( std::initializer_list<std::pair<const std::string, JSONValue>> args) : std::map<std::string, JSONValue>(args) {}

        JSON( std::initializer_list<std::pair<const std::string, JSONValue>> args, std::vector<std::pair<const std::string, JSONValue>> more_args ) : std::map<std::string, JSONValue>(args) {
            for (auto& pair : more_args) {
                (*this)[pair.first] = pair.second;
            }
        }
    };

    // Declarations that we expose
    uint64_t get_random();
}

#if defined(NO_ANTITHESIS_SDK) || __cplusplus < 202000L || (defined(__clang__) && __clang_major__ < 16)

#if __cplusplus < 202000L
    #error "The Antithesis C++ API requires C++20 or higher"
#endif
#if defined(__clang__) && __clang_major__ < 16
    #error "The Antithesis C++ API requires clang version 16 or higher"
#endif

#define ALWAYS(cond, message, ...)
#define ALWAYS_OR_UNREACHABLE(cond, message, ...)
#define SOMETIMES(cond, message, ...)
#define REACHABLE(message, ...)
#define UNREACHABLE(message, ...)
#define ALWAYS_GREATER_THAN(val, threshold, message, ...)
#define ALWAYS_GREATER_THAN_OR_EQUAL_TO(val, threshold, message, ...)
#define SOMETIMES_GREATER_THAN(val, threshold, message, ...)
#define SOMETIMES_GREATER_THAN_OR_EQUAL_TO(val, threshold, message, ...)
#define ALWAYS_LESS_THAN(val, threshold, message, ...)
#define ALWAYS_LESS_THAN_OR_EQUAL_TO(val, threshold, message, ...)
#define SOMETIMES_LESS_THAN(val, threshold, message, ...)
#define SOMETIMES_LESS_THAN_OR_EQUAL_TO(val, threshold, message, ...)
#define ALWAYS_SOME(pairs, message, ...)
#define SOMETIMES_ALL(pairs, message, ...)

namespace antithesis {
    inline uint64_t get_random() {
        static LocalRandom random_gen;
        return random_gen.random();
    }

    inline void setup_complete(const JSON& details) {
    }

    inline void send_event(const char* name, const JSON& details) {
    }
}

#else

#include <cstdio>
#include <array>
#include <source_location>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <dlfcn.h>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace antithesis {
    constexpr const char* const ERROR_LOG_LINE_PREFIX = "[* antithesis-sdk-cpp *]";
    constexpr const char* LIB_PATH = "/usr/lib/libvoidstar.so";
    constexpr const char* LOCAL_OUTPUT_ENVIRONMENT_VARIABLE = "ANTITHESIS_SDK_LOCAL_OUTPUT";
    
    static std::ostream& operator<<(std::ostream& out, const JSON& details);

    struct LibHandler {
        virtual ~LibHandler() = default;
        virtual void output(const char* message) const = 0;
        virtual uint64_t random() = 0;

        void output(const JSON& json) const {
            std::ostringstream out;
            out << json;
            output(out.str().c_str());
        }
    };

    struct AntithesisHandler : LibHandler {
        void output(const char* message) const override {
            if (message != nullptr) {
                fuzz_json_data(message, strlen(message));
                fuzz_flush();
            }
        }

        uint64_t random() override {
            return fuzz_get_random();
        }

        static std::unique_ptr<AntithesisHandler> create() {
            void* shared_lib = dlopen(LIB_PATH, RTLD_NOW);
            if (!shared_lib) {
                error("Can not load the Antithesis native library");
                return nullptr;
            }

            void* fuzz_json_data = dlsym(shared_lib, "fuzz_json_data");
            if (!fuzz_json_data) {
                error("Can not access symbol fuzz_json_data");
                return nullptr;
            }

            void* fuzz_flush = dlsym(shared_lib, "fuzz_flush");
            if (!fuzz_flush) {
                error("Can not access symbol fuzz_flush");
                return nullptr;
            }

            void* fuzz_get_random = dlsym(shared_lib, "fuzz_get_random");
            if (!fuzz_get_random) {
                error("Can not access symbol fuzz_get_random");
                return nullptr;
            }

            return std::unique_ptr<AntithesisHandler>(new AntithesisHandler(
                reinterpret_cast<fuzz_json_data_t>(fuzz_json_data),
                reinterpret_cast<fuzz_flush_t>(fuzz_flush),
                reinterpret_cast<fuzz_get_random_t>(fuzz_get_random)));
        }

    private:
        typedef void (*fuzz_json_data_t)( const char* message, size_t length );
        typedef void (*fuzz_flush_t)();
        typedef uint64_t (*fuzz_get_random_t)();


        fuzz_json_data_t fuzz_json_data;
        fuzz_flush_t fuzz_flush;
        fuzz_get_random_t fuzz_get_random;

        AntithesisHandler(fuzz_json_data_t fuzz_json_data, fuzz_flush_t fuzz_flush, fuzz_get_random_t fuzz_get_random) :
            fuzz_json_data(fuzz_json_data), fuzz_flush(fuzz_flush), fuzz_get_random(fuzz_get_random) {}

        static void error(const char* message) {
            fprintf(stderr, "%s %s: %s\n", ERROR_LOG_LINE_PREFIX, message, dlerror());
        }
    };

    struct LocalHandler : LibHandler{
        ~LocalHandler() override {
            if (file != nullptr) {
                fclose(file);
            }
        }

        void output(const char* message) const override {
            if (file != nullptr && message != nullptr) {
                fprintf(file, "%s\n", message);
            }
        }

        uint64_t random() override {
            return random_gen.random();
        }

        static std::unique_ptr<LocalHandler> create() {
            return std::unique_ptr<LocalHandler>(new LocalHandler(create_internal()));
        }
    private:
        FILE* file;
        LocalRandom random_gen;

        LocalHandler(FILE* file): file(file), random_gen() {
        }

        // If `localOutputEnvVar` is set to a non-empty path, attempt to open that path and truncate the file
        // to serve as the log file of the local handler.
        // Otherwise, we don't have a log file, and logging is a no-op in the local handler.
        static FILE* create_internal() {
            const char* path = std::getenv(LOCAL_OUTPUT_ENVIRONMENT_VARIABLE);
            if (!path || !path[0]) {
                return nullptr;
            }

            // Open the file for writing (create if needed and possible) and truncate it
            FILE* file = fopen(path, "w");
            if (file == nullptr) {
                fprintf(stderr, "%s Failed to open path %s: %s\n", ERROR_LOG_LINE_PREFIX, path, strerror(errno));
                return nullptr;
            }
            int ret = fchmod(fileno(file), 0644);
            if (ret != 0) {
                fprintf(stderr, "%s Failed to set permissions for path %s: %s\n", ERROR_LOG_LINE_PREFIX, path, strerror(errno));
                fclose(file);
                return nullptr;
            }

            return file;
        }
    };

    static std::unique_ptr<LibHandler> init() {
        struct stat stat_buf;
        if (stat(LIB_PATH, &stat_buf) == 0) {
            std::unique_ptr<LibHandler> tmp = AntithesisHandler::create();
            if (!tmp) {
                fprintf(stderr, "%s Failed to create handler for Antithesis library\n", ERROR_LOG_LINE_PREFIX);
                exit(-1);
            }
            return tmp;
        } else {
            return LocalHandler::create();
        }
    }

    struct AssertionState {
        uint8_t false_not_seen : 1;
        uint8_t true_not_seen : 1;
        uint8_t rest : 6;

        AssertionState() : false_not_seen(true), true_not_seen(true), rest(0)  {}
    };

    template<class>
    inline constexpr bool always_false_v = false;

    static std::ostream& operator<<(std::ostream& out, const JSONValue& json) {
        std::visit([&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                out << std::quoted(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                out << (arg ? "true" : "false");
            } else if constexpr (std::is_same_v<T, char>) {
                char tmp[2] = {arg, '\0'};
                out << std::quoted(tmp);
            } else if constexpr (std::is_same_v<T, int>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, float>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, double>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, const char*>) {
                out << std::quoted(arg);
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out << "null";
            } else if constexpr (std::is_same_v<T, JSON>) {
                out << arg;
            } else if constexpr (std::is_same_v<T, JSONArray>) {
                out << '[';
                bool first = true;
                for (auto &item : arg) {
                    if (!first) {
                        out << ',';
                    }
                    first = false;
                    out << item;
                }
                out << ']';
            } else {
                static_assert(always_false_v<T>, "non-exhaustive JSONValue visitor!");
            }
        }, json);

        return out;
    }

    static std::ostream& operator<<(std::ostream& out, const JSON& details) {
        out << '{';

        bool first = true;
        for (auto [key, value] : details) {
            if (!first) {
                out << ',';
            }
            out << std::quoted(key) << ':' << value;
            first = false;
        }

        out << '}';
        return out;
    }

    enum AssertionType {
        ALWAYS_ASSERTION,
        ALWAYS_OR_UNREACHABLE_ASSERTION,
        SOMETIMES_ASSERTION,
        REACHABLE_ASSERTION,
        UNREACHABLE_ASSERTION,
    };

    inline constexpr bool get_must_hit(AssertionType type) {
        switch (type) {
            case ALWAYS_ASSERTION:
            case SOMETIMES_ASSERTION: 
            case REACHABLE_ASSERTION:
                return true;
            case ALWAYS_OR_UNREACHABLE_ASSERTION: 
            case UNREACHABLE_ASSERTION: 
                return false;
        }
    }

    inline constexpr const char* get_assert_type_string(AssertionType type) {
        switch (type) {
            case ALWAYS_ASSERTION:
            case ALWAYS_OR_UNREACHABLE_ASSERTION: 
                return "always";
            case SOMETIMES_ASSERTION: 
                return "sometimes";
            case REACHABLE_ASSERTION:
            case UNREACHABLE_ASSERTION: 
                return "reachability";
        }
    }

    inline constexpr const char* get_display_type_string(AssertionType type) {
        switch (type) {
            case ALWAYS_ASSERTION: return "Always";
            case ALWAYS_OR_UNREACHABLE_ASSERTION: return "AlwaysOrUnreachable";
            case SOMETIMES_ASSERTION: return "Sometimes";
            case REACHABLE_ASSERTION: return "Reachable";
            case UNREACHABLE_ASSERTION: return "Unreachable";
        }
    }

    struct LocationInfo {
        const char* class_name;
        const char* function_name;
        const char* file_name;
        const int line;
        const int column;

        JSON to_json() const {
            return JSON{
                {"class", class_name},
                {"function", function_name},
                {"file", file_name},
                {"begin_line", line},
                {"begin_column", column},
            };
        }
    };

    inline std::string make_key([[maybe_unused]] const char* message, const LocationInfo& location_info) {
        return message;
    }

    inline LibHandler& get_lib_handler() {
        static LibHandler* lib_handler = nullptr;
        if (lib_handler == nullptr) {
            lib_handler = init().release(); // Leak on exit, rather than exit-time-destructor

            JSON language_block{
              {"name", "C++"},
              {"version", __VERSION__}
            };

            JSON version_message{
                {"antithesis_sdk", JSON{
                    {"language", language_block},
                    {"sdk_version", SDK_VERSION},
                    {"protocol_version", PROTOCOL_VERSION}
                }
            }};
            lib_handler->output(version_message);
        }

        return *lib_handler;
    }

    inline void assert_impl(bool cond, const char* message, const JSON& details, const LocationInfo& location_info,
                    bool hit, bool must_hit, const char* assert_type, const char* display_type, const char* id) {
        JSON assertion{
            {"antithesis_assert", JSON{
                {"hit", hit},
                {"must_hit", must_hit},
                {"assert_type", assert_type},
                {"display_type", display_type},
                {"message", message},
                {"condition", cond},
                {"id", id},
                {"location", location_info.to_json()},
                {"details", details},
            }}
        };
        get_lib_handler().output(assertion);
    }

    inline void assert_raw(bool cond, const char* message, const JSON& details, 
                            const char* class_name, const char* function_name, const char* file_name, const int line, const int column,     
                            bool hit, bool must_hit, const char* assert_type, const char* display_type, const char* id) {
        LocationInfo location_info{ class_name, function_name, file_name, line, column };
        assert_impl(cond, message, details, location_info, hit, must_hit, assert_type, display_type, id);
    }

    typedef std::set<std::string> CatalogEntryTracker;

    inline CatalogEntryTracker& get_catalog_entry_tracker() {
        static CatalogEntryTracker catalog_entry_tracker;
        return catalog_entry_tracker;
    }

    struct Assertion {
        AssertionState state;
        AssertionType type;
        const char* message;
        LocationInfo location;

        Assertion(const char* message, AssertionType type, LocationInfo&& location) : 
            state(), type(type), message(message), location(std::move(location)) { 
            this->add_to_catalog();
        }

        void add_to_catalog() const {
            std::string id = make_key(message, location);
            CatalogEntryTracker& tracker = get_catalog_entry_tracker();
            if (!tracker.contains(id)) {
                tracker.insert(id);
                const bool condition = (type == REACHABLE_ASSERTION ? true : false);
                const bool hit = false;
                const char* assert_type = get_assert_type_string(type);
                const bool must_hit = get_must_hit(type);
                const char* display_type = get_display_type_string(type);
                assert_impl(condition, message, {}, location, hit, must_hit, assert_type, display_type, id.c_str());
            }
        }

        [[clang::always_inline]] inline void check_assertion(bool cond, const JSON& details) {
            #if defined(NO_ANTITHESIS_SDK)
              #error "Antithesis SDK has been disabled"
            #endif
            if (__builtin_expect(state.false_not_seen || state.true_not_seen, false)) {
                check_assertion_internal(cond, details);
            }
        }

        private:
        void check_assertion_internal(bool cond, const JSON& details) {
            bool emit = false;
            if (!cond && state.false_not_seen) {
                emit = true;
                state.false_not_seen = false;   // TODO: is the race OK?
            }

            if (cond && state.true_not_seen) {
                emit = true;
                state.true_not_seen = false;   // TODO: is the race OK?
            }

            if (emit) {
                const bool hit = true;
                const char* assert_type = get_assert_type_string(type);
                const bool must_hit = get_must_hit(type);
                const char* display_type = get_display_type_string(type);
                std::string id = make_key(message, location);
                assert_impl(cond, message, details, location, hit, must_hit, assert_type, display_type, id.c_str());
            }
        }
    };

    inline uint64_t get_random() {
        return get_lib_handler().random();
    }

    inline void setup_complete(const JSON& details) {
        JSON json{ 
            { "antithesis_setup", JSON{ 
                {"status", "complete"}, 
                {"details", details}
            }} 
        };
        get_lib_handler().output(json);
    }

    inline void send_event(const char* name, const JSON& details) {
        JSON json = { { name, details } };
        get_lib_handler().output(json);
    }

    enum GuidepostType {
        GUIDEPOST_MAXIMIZE,
        GUIDEPOST_MINIMIZE,
        GUIDEPOST_EXPLORE,
        GUIDEPOST_ALL,
        GUIDEPOST_NONE
    };

    inline constexpr const char* get_guidance_type_string(GuidepostType type) {
        switch (type) {
            case GUIDEPOST_MAXIMIZE:
            case GUIDEPOST_MINIMIZE:
                return "numeric";
            case GUIDEPOST_ALL:
            case GUIDEPOST_NONE:
                return "boolean";
            case GUIDEPOST_EXPLORE:
                return "json";
        }
    }

    inline constexpr bool does_guidance_maximize(GuidepostType type) {
        switch (type) {
            case GUIDEPOST_MAXIMIZE:
            case GUIDEPOST_ALL:
                return true;
            case GUIDEPOST_EXPLORE:
            case GUIDEPOST_MINIMIZE:
            case GUIDEPOST_NONE:
                return false;
        }
    }

    template <typename NumericValue, class Value=std::pair<NumericValue, NumericValue>>
    struct NumericGuidepost {
        const char* message;
        LocationInfo location;
        GuidepostType type;
        // an approximation of (left - right) / 2; contains an absolute value and a sign bit
        std::pair<NumericValue, bool> extreme_half_gap;

        NumericGuidepost(const char* message, LocationInfo&& location, GuidepostType type) :
            message(message), location(std::move(location)), type(type) {
                this->add_to_catalog();
                if (type == GUIDEPOST_MAXIMIZE) {
                    extreme_half_gap = { std::numeric_limits<NumericValue>::max(), false }; 
                } else {
                    extreme_half_gap = { std::numeric_limits<NumericValue>::max(), true };
                }
            }

        inline void add_to_catalog() {
            std::string id = make_key(message, location);
            JSON catalog{
                {"antithesis_guidance", JSON{
                    {"guidance_type", get_guidance_type_string(type)},
                    {"message", message},
                    {"id", id},
                    {"location", location.to_json()},
                    {"maximize", does_guidance_maximize(type)},
                    {"hit", false}
                }}
            };
            get_lib_handler().output(catalog);
        }

        std::pair<NumericValue, bool> compute_half_gap(NumericValue left, NumericValue right) {
            // An extremely baroque way to compute (left - right) / 2, rounded toward 0, without overflowing or underflowing
            if (std::is_integral_v<NumericValue>) {
                // If both numbers are odd then the gap doesn't change if we subtract 1 from both sides
                // Also subtracting 1 from both sides won't underflow
                if (left % 2 == 1 && right % 2 == 1) 
                    return compute_half_gap( left - 1, right - 1);
                // If one number is odd then we subtract 1 from the larger number
                // This rounds the computation toward 0 but again won't underflow
                if (left % 2 == 1 || right % 2 == 1) {
                    if (left > right) {
                        return compute_half_gap( left - 1, right );
                    } else {
                        return compute_half_gap( left, right - 1 );
                    }
                }
                // At this point both numbers are even, so the midpoint calculation is exact
                NumericValue half_left = left / 2;
                NumericValue half_right = right / 2;
                NumericValue midpoint = half_left + half_right;
                // This won't overflow or underflow because we're subtracting the midpoint
                // We compute a positive value and a sign so that we don't have to do weird things with unsigned types
                if (left > right) {
                    return { midpoint - right, true };
                } else {
                    return { right - midpoint, false };
                }
            } else {
                // If it's floating point we don't need to worry about overflowing, just do the arithmetic
                return { left > right ? (left - right) / 2 : (right - left) / 2, left > right };
            }
        }

        bool should_send_value(std::pair<NumericValue, bool> half_gap) {
            if (this->type == GUIDEPOST_MAXIMIZE) {
                if (half_gap.second && !extreme_half_gap.second) {
                    // we're positive and the extreme value isn't; always send back
                    return true;
                } else if (!half_gap.second && extreme_half_gap.second) {
                    // we're negative and the extreme value is positive; never send back
                    return false;
                } else if (half_gap.second && extreme_half_gap.second) {
                    // both positive; send back if our absolute value is at least as large
                    return half_gap.first >= extreme_half_gap.first;
                } else {
                    // both negative; send back if our absolute value is at least as small
                    return half_gap.first <= extreme_half_gap.first;
                }
            } else {
                if (half_gap.second && !extreme_half_gap.second) {
                    // we're positive and the extreme value isn't; never send back
                    return false;
                } else if (!half_gap.second && extreme_half_gap.second) {
                    // we're negative and the extreme value is positive; always send back
                    return true;
                } else if (half_gap.second && extreme_half_gap.second) {
                    // both positive; send back if our absolute value is at least as small
                    return half_gap.first <= extreme_half_gap.first;
                } else {
                    // both negative; send back if our absolute value is at least as large
                    return half_gap.first >= extreme_half_gap.first;
                }
            }
        }

        [[clang::always_inline]] inline void send_guidance(Value value) {
            std::pair<NumericValue, bool> half_gap = compute_half_gap(value.first, value.second);
            if (should_send_value(half_gap)) {
                extreme_half_gap = half_gap;
                std::string id = make_key(this->message, this->location);
                JSON guidance{
                    {"antithesis_guidance", JSON{
                        {"guidance_type", get_guidance_type_string(this->type)},
                        {"message", this->message},
                        {"id", id},
                        {"location", this->location.to_json()},
                        {"maximize", does_guidance_maximize(this->type)},
                        {"guidance_data", JSON{ 
                            { "left", value.first },    
                            { "right", value.second } }},
                        {"hit", true}
                    }}
                };
                get_lib_handler().output(guidance);
            }
        }   
    };

    template <typename GuidanceType>
    struct BooleanGuidepost {
        const char* message;
        LocationInfo location;
        GuidepostType type;

        BooleanGuidepost(const char* message, LocationInfo&& location, GuidepostType type) :
            message(message), location(std::move(location)), type(type) {
                this->add_to_catalog();
            }

        inline void add_to_catalog() {
            std::string id = make_key(message, location);
            JSON catalog{
                {"antithesis_guidance", JSON{
                    {"guidance_type", get_guidance_type_string(type)},
                    {"message", message},
                    {"id", id},
                    {"location", location.to_json()},
                    {"maximize", does_guidance_maximize(type)},
                    {"hit", false}
                }}
            };
            get_lib_handler().output(catalog);
        }

        inline virtual void send_guidance(GuidanceType data) {
            std::string id = make_key(this->message, this->location);
            JSON guidance{
                {"antithesis_guidance", JSON{
                    {"guidance_type", get_guidance_type_string(this->type)},
                    {"message", this->message},
                    {"id", id},
                    {"location", location.to_json()},
                    {"maximize", does_guidance_maximize(this->type)},
                    {"guidance_data", data},
                    {"hit", true}
                }}
            };
            get_lib_handler().output(guidance);
        }
    };
}

namespace {
    template <std::size_t N>
    struct fixed_string {
        std::array<char, N> contents;
        constexpr fixed_string() {
            for(unsigned int i=0; i<N; i++) contents[i] = 0;
        }

        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
        constexpr fixed_string( const char (&arr)[N] )
        {
            for(unsigned int i=0; i<N; i++) contents[i] = arr[i];
        }

        static constexpr fixed_string<N> from_c_str( const char* s ) {
            fixed_string<N> it;
            for(unsigned int i=0; i<N && s[i]; i++)
                it.contents[i] = s[i];
            return it;
        }
        #pragma clang diagnostic pop

        const char* c_str() const { return contents.data(); }
    };

    template <std::size_t N>
    fixed_string( const char (&arr)[N] ) -> fixed_string<N>;

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    static constexpr size_t string_length( const char * s ) {
        for(int l = 0; ; l++)
            if (!s[l])
                return l;
    }
    #pragma clang diagnostic pop

    template <antithesis::AssertionType type, fixed_string message, fixed_string file_name, fixed_string function_name, int line, int column>
    struct CatalogEntry {
        [[clang::always_inline]] static inline antithesis::Assertion create() {
            antithesis::LocationInfo location{ "", function_name.c_str(), file_name.c_str(), line, column };
            return antithesis::Assertion(message.c_str(), type, std::move(location));
        }

        static inline antithesis::Assertion assertion = create();
    };

    template<typename GuidanceDataType, antithesis::GuidepostType type, fixed_string message, fixed_string file_name, fixed_string function_name, int line, int column>
    struct BooleanGuidanceCatalogEntry {
        [[clang::always_inline]] static inline antithesis::BooleanGuidepost<GuidanceDataType> create() {
            antithesis::LocationInfo location{ "", function_name.c_str(), file_name.c_str(), line, column };
            switch (type) {
                case antithesis::GUIDEPOST_ALL:
                case antithesis::GUIDEPOST_NONE:
                    return antithesis::BooleanGuidepost<GuidanceDataType>(message.c_str(), std::move(location), type);
                default:
                    throw std::runtime_error("Can't create boolean guidepost with non-boolean type");
            }
        }
        
        static inline antithesis::BooleanGuidepost<GuidanceDataType> guidepost = create();
    };

    template<typename NumericType, antithesis::GuidepostType type, fixed_string message, fixed_string file_name, fixed_string function_name, int line, int column>
    struct NumericGuidanceCatalogEntry {
        [[clang::always_inline]] static inline antithesis::NumericGuidepost<NumericType> create() {
            antithesis::LocationInfo location{ "", function_name.c_str(), file_name.c_str(), line, column };
            switch (type) {
                case antithesis::GUIDEPOST_MAXIMIZE:
                case antithesis::GUIDEPOST_MINIMIZE:
                    return antithesis::NumericGuidepost<NumericType>(message.c_str(), std::move(location), type);
                default:
                    throw std::runtime_error("Can't create numeric guidepost with non-numeric type");
            }
        }
        
        static inline antithesis::NumericGuidepost<NumericType> guidepost = create();
    };
}

#define FIXED_STRING_FROM_C_STR(s) (fixed_string<string_length(s)+1>::from_c_str(s))

#define _NL_1(foo) { #foo, foo }
#define _NL_2(foo, ...) { #foo, foo }, _NL_1(__VA_ARGS__)
#define _NL_3(foo, ...) { #foo, foo }, _NL_2(__VA_ARGS__)
#define _NL_4(foo, ...) { #foo, foo }, _NL_3(__VA_ARGS__)
#define _NL_5(foo, ...) { #foo, foo }, _NL_4(__VA_ARGS__)
#define _NL_6(foo, ...) { #foo, foo }, _NL_5(__VA_ARGS__)
#define _NL_7(foo, ...) { #foo, foo }, _NL_6(__VA_ARGS__)
#define _NL_8(foo, ...) { #foo, foo }, _NL_7(__VA_ARGS__)
#define _NL_9(foo, ...) { #foo, foo }, _NL_8(__VA_ARGS__)
#define _NL_10(foo, ...) { #foo, foo }, _NL_9(__VA_ARGS__)

#define _ELEVENTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N

#define _GET_NL(...) \
    _ELEVENTH_ARG(__VA_ARGS__, _NL_10, _NL_9, _NL_8, _NL_7, _NL_6, _NL_5, _NL_4, _NL_3, _NL_2, _NL_1)

#define NAMED_LIST(...) { _GET_NL(__VA_ARGS__)(__VA_ARGS__) }

#define ANTITHESIS_ASSERT_RAW(type, cond, message, ...) ( \
    CatalogEntry< \
        type, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(cond, (antithesis::JSON(__VA_ARGS__)) ) )

#define ALWAYS(cond, message, ...) ANTITHESIS_ASSERT_RAW(antithesis::ALWAYS_ASSERTION, cond, message, __VA_ARGS__)
#define ALWAYS_OR_UNREACHABLE(cond, message, ...) ANTITHESIS_ASSERT_RAW(antithesis::ALWAYS_OR_UNREACHABLE_ASSERTION, cond, message, __VA_ARGS__)
#define SOMETIMES(cond, message, ...) ANTITHESIS_ASSERT_RAW(antithesis::SOMETIMES_ASSERTION, cond, message, __VA_ARGS__)
#define REACHABLE(message, ...) ANTITHESIS_ASSERT_RAW(antithesis::REACHABLE_ASSERTION, true, message, __VA_ARGS__)
#define UNREACHABLE(message, ...) ANTITHESIS_ASSERT_RAW(antithesis::UNREACHABLE_ASSERTION, false, message, __VA_ARGS__)

#define ALWAYS_GREATER_THAN(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in ALWAYS_GREATER_THAN must be of same type"); \
    CatalogEntry< \
        antithesis::ALWAYS_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left > right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MINIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define ALWAYS_GREATER_THAN_OR_EQUAL_TO(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in ALWAYS_GREATER_THAN_OR_EQUAL_TO must be of same type"); \
    CatalogEntry< \
        antithesis::ALWAYS_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left >= right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MINIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define SOMETIMES_GREATER_THAN(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in SOMETIMES_GREATER_THAN must be of same type"); \
    CatalogEntry< \
        antithesis::SOMETIMES_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left > right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MAXIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define SOMETIMES_GREATER_THAN_OR_EQUAL_TO(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in SOMETIMES_GREATER_THAN_OR_EQUAL_TO must be of same type"); \
    CatalogEntry< \
        antithesis::SOMETIMES_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left >= right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MAXIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define ALWAYS_LESS_THAN(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in ALWAYS_LESS_THAN must be of same type"); \
    CatalogEntry< \
        antithesis::ALWAYS_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left < right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MAXIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define ALWAYS_LESS_THAN_OR_EQUAL_TO(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in ALWAYS_LESS_THAN_OR_EQUAL_TO must be of same type"); \
    CatalogEntry< \
        antithesis::ALWAYS_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left <= right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MAXIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define SOMETIMES_LESS_THAN(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in SOMETIMES_LESS_THAN must be of same type"); \
    CatalogEntry< \
        antithesis::SOMETIMES_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left < right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MINIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define SOMETIMES_LESS_THAN_OR_EQUAL_TO(left, right, message, ...) \
do { \
    static_assert(std::is_same_v<decltype(left), decltype(right)>, "Values compared in SOMETIMES_LESS_THAN_OR_EQUAL_TO must be of same type"); \
    CatalogEntry< \
        antithesis::SOMETIMES_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(left <= right, (antithesis::JSON(__VA_ARGS__, {{ "left", left }, { "right", right }})) ); \
    NumericGuidanceCatalogEntry< \
        decltype(left), \
        antithesis::GUIDEPOST_MINIMIZE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance({ left, right }); \
} while (0)

#define ALWAYS_SOME(pairs, message, ...) \
do { \
    bool disjunction = false; \
    for (std::pair<std::string, bool> pair : pairs) { \
        if (pair.second) { \
            disjunction = true; \
            break; \
        } \
    } \
    CatalogEntry< \
        antithesis::ALWAYS_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(disjunction, (antithesis::JSON(__VA_ARGS__, pairs)) ); \
    antithesis::json json_pairs = antithesis::json(pairs); \
    BooleanGuidanceCatalogEntry< \
        decltype(json_pairs), \
        antithesis::GUIDEPOST_NONE, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance(json_pairs); \
} while (0)

#define SOMETIMES_ALL(pairs, message, ...) \
do { \
    bool conjunction = true; \
    for (std::pair<std::string, bool> pair : pairs) { \
        if (!pair.second) { \
            conjunction = false; \
            break; \
        } \
    } \
    CatalogEntry< \
        antithesis::SOMETIMES_ASSERTION, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::assertion.check_assertion(conjunction, (antithesis::JSON(__VA_ARGS__, pairs)) ); \
    antithesis::json json_pairs = antithesis::json(pairs); \
    BooleanGuidanceCatalogEntry< \
        decltype(json_pairs), \
        antithesis::GUIDEPOST_ALL, \
        fixed_string(message), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().file_name()), \
        FIXED_STRING_FROM_C_STR(std::source_location::current().function_name()), \
        std::source_location::current().line(), \
        std::source_location::current().column() \
    >::guidepost.send_guidance(json_pairs); \
} while (0)

#endif

namespace antithesis {
    template <typename Iter>
    Iter random_choice(Iter begin, Iter end) {
        ssize_t num_things = end - begin;
        if (num_things == 0) {
            return end;
        }

        uint64_t uval = get_random();
        ssize_t index = uval % num_things;
        return begin + index;
    }
}
