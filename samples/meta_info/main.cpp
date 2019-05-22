#include "pch.h"

using namespace std::chrono;
using namespace std::string_view_literals;
using namespace xlang;
using namespace xlang::meta::reader;
using namespace xlang::text;
using namespace xlang::cmd;

template <typename...T> struct overloaded : T... { using T::operator()...; };
template <typename...T> overloaded(T...)->overloaded<T...>;

struct writer : writer_base<writer>
{
    using writer_base<writer>::write;

    struct indent_guard
    {
        explicit indent_guard(writer& w, int32_t offset = 1) noexcept : _writer(w), _offset(offset)
        {
            _writer.indent += _offset;
        }

        ~indent_guard()
        {
            _writer.indent -= _offset;
        }

    private:
        writer& _writer;
        int32_t _offset;
    };

    std::string_view current;
    int32_t indent{};

    void write_indent()
    {
        for (int32_t i = 0; i < indent; i++)
        {
            writer_base::write_impl("    ");
        }
    }

    void write_impl(std::string_view const& value)
    {
        std::string_view::size_type current_pos{ 0 };
        auto on_new_line = back() == '\n';

        while (true)
        {
            const auto pos = value.find('\n', current_pos);

            if (pos == std::string_view::npos)
            {
                if (current_pos < value.size())
                {
                    if (on_new_line)
                    {
                        write_indent();
                    }

                    writer_base::write_impl(value.substr(current_pos));
                }

                return;
            }

            auto current_line = value.substr(current_pos, pos - current_pos + 1);
            auto empty_line = current_line[0] == '\n';

            if (on_new_line && !empty_line)
            {
                write_indent();
            }

            writer_base::write_impl(current_line);

            on_new_line = true;
            current_pos = pos + 1;
        }
    }

    void write_impl(char const value)
    {
        if (back() == '\n' && value != '\n')
        {
            write_indent();
        }

        writer_base::write_impl(value);
    }

    template <typename... Args>
    std::string write_temp(std::string_view const& value, Args const& ... args)
    {
        auto restore_indent = indent;
        indent = 0;

        auto result = writer_base::write_temp(value, args...);

        indent = restore_indent;

        return result;
    }
};

void print_usage()
{
    puts("Usage...");
}

int main(int const argc, char** argv)
{
    writer w;

    try
    {
        auto start = high_resolution_clock::now();

        static constexpr cmd::option options[]
        {
            // name, min, max
            { "input", 1 },
//            { "output", 0, 1 },
            { "include", 0 },
            { "exclude", 0 },
            { "verbose", 0, 0 },
        };

        reader args{ argc, argv, options };

        if (!args)
        {
            print_usage();
            return 0;
        }

        cache c{ args.values("input") };
        bool const verbose = args.exists("verbose");

        filter f{ args.values("include"), args.values("exclude") };

        if (verbose)
        {
            std::for_each(c.databases().begin(), c.databases().end(), [&](auto&& db)
            {
                w.write("in: %\n", db.path());
            });
        }

        w.flush_to_console();

        size_t num_namespaces{};
        size_t num_interfaces{};
        size_t num_classes{};
        size_t num_structs{};
        size_t num_enums{};
        size_t num_delegates{};
        size_t num_attributes{};
        size_t num_contracts{};

        for (auto const& [ns, members] : c.namespaces())
        {
            num_namespaces++;
            num_interfaces += members.interfaces.size();
            num_classes += members.classes.size();
            num_structs += members.structs.size();
            num_enums += members.enums.size();
            num_delegates += members.delegates.size();
            num_attributes += members.attributes.size();
            num_contracts += members.contracts.size();
        }

        w.write("Namespaces     %\n", num_namespaces);
        w.write("Interfaces     %\n", num_interfaces);
        w.write("Classes        %\n", num_classes);
        w.write("Structs        %\n", num_structs);
        w.write("Enums          %\n", num_enums);
        w.write("Delegates      %\n", num_delegates);
        w.write("Attributes     %\n", num_attributes);
        w.write("Contracts      %\n", num_contracts);

        if (verbose)
        {
            w.write("time: %ms\n", duration_cast<duration<int64_t, std::milli>>(high_resolution_clock::now() - start).count());
        }
    }
    catch (std::exception const& e)
    {
        w.write("%\n", e.what());
    }

    w.flush_to_console();
}
