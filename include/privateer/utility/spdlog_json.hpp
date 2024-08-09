
namespace utility{
    struct spdlog_json{
        {
            using json_val = std::variant<std::int64_t, int, double, std::string, bool>;
            std::unordered_map<std::string, json_val> members;

            spdlog_json(std::initializer_list<std::pair<const std::string, json_val>> il) : members{il} {}

            template<typename OStream>
                friend OStream &operator<<(OStream &os, const spdlog_json &j)
            {
                for (const auto &kv : j.members) {
                    os << ", " << std::quoted(kv.first) << ":";
                    std::visit(overloaded {
                        [&](std::int64_t arg) { os << arg; },
                        [&](int arg) { os << arg; },
                        [&](double arg) { os << arg; },
                        [&](const std::string& arg) { os << std::quoted(arg); },
                        [&](bool arg) { os << (arg ? "true" : "false"); }
                    }, kv.second);
                }
                return os;
            }
        };
    }
}
