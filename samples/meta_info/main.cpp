#include "pch.h"

using namespace std::chrono;
using namespace std::string_view_literals;
using namespace xlang;
using namespace xlang::meta::reader;
using namespace xlang::text;
using namespace xlang::cmd;

template <typename...T> struct overloaded : T... { using T::operator()...; };
template <typename...T> overloaded(T...)->overloaded<T...>;


namespace meta_info
{

struct usage_exception {};

static constexpr cmd::option options[]
{
    { "input",              0, cmd::option::no_max, "<spec>", "Windows metadata to display info for" },
    { "include",            0, cmd::option::no_max, "<prefix>", "One or more prefixes to include in input" },
    { "exclude",            0, cmd::option::no_max, "<prefix>", "One or more prefixes to exclude from input" },
    { "group-lists",        0, 0, {}, "Group lists hierarchically" },
    { "extra-details",      0, 0, {}, "Display more details for types" },
    { "list-all",           0, 0, {}, "List all types" },
    { "list-namespaces",    0, 0, {}, "List namespaces" },
    { "list-interfaces",    0, 0, {}, "List interfaces" },
    { "list-methods",       0, 0, {}, "List methods" },
    { "list-properties",    0, 0, {}, "List properties" },
    { "list-events",        0, 0, {}, "List events" },
    { "list-fields",        0, 0, {}, "List fields" },
    { "list-classes",       0, 0, {}, "List classes" },
    { "list-structs",       0, 0, {}, "List structs" },
    { "list-enums",         0, 0, {}, "List enums" },
    { "list-delegates",     0, 0, {}, "List delegates" },
    { "list-attributes",    0, 0, {}, "List attributes" },
    { "list-contracts",     0, 0, {}, "List contracts" },
    { "verbose",            0, 0, {}, "Show detailed information" },
    { "help",               0, 0, {}, "Show detailed help" }
};


struct settings_type
{
    std::set<std::string> input;
    std::set<std::string> include;
    std::set<std::string> exclude;
    filter filter;
    bool verbose{};
    bool group_lists;
    bool extra_details;
    bool list_namespaces;
    bool list_interfaces;
    bool list_methods;
    bool list_properties;
    bool list_events;
    bool list_fields;
    bool list_classes;
    bool list_structs;
    bool list_enums;
    bool list_delegates;
    bool list_attributes;
    bool list_contracts;
} settings;


struct meta_summary
{
    meta_summary(std::string_view const& title) noexcept : title{ title } {}
    meta_summary(std::string_view const& ns, cache::namespace_members const& members) noexcept : title{ ns }, single_namespace{ true }
    {
        num_namespaces = 1;
        num_interfaces = members.interfaces.size();
        num_classes = members.classes.size();
        num_structs = members.structs.size();
        num_enums = members.enums.size();
        num_delegates = members.delegates.size();
        num_attributes = members.attributes.size();
        num_contracts = members.contracts.size();

        for (auto const& iface : members.interfaces)
        {
            auto const& methods = iface.MethodList();
            auto const& properties = iface.PropertyList();
            auto const& events = iface.EventList();
            auto const& fields = iface.FieldList();

            num_methods += distance(methods);
            num_properties += distance(properties);
            num_events += distance(events);
            num_fields += distance(fields);
        }
    }

    const std::string title;
    const bool single_namespace{};
    size_t num_namespaces{};
    size_t num_interfaces{};
    size_t num_methods{};
    size_t num_properties{};
    size_t num_events{};
    size_t num_fields{};
    size_t num_classes{};
    size_t num_structs{};
    size_t num_enums{};
    size_t num_delegates{};
    size_t num_attributes{};
    size_t num_contracts{};

    meta_summary& operator +=(meta_summary const& other) noexcept
    {
        num_namespaces += other.num_namespaces;
        num_interfaces += other.num_interfaces;
        num_methods += other.num_methods;
        num_properties += other.num_properties;
        num_events += other.num_events;
        num_fields += other.num_fields;
        num_classes += other.num_classes;
        num_structs += other.num_structs;
        num_enums += other.num_enums;
        num_delegates += other.num_delegates;
        num_attributes += other.num_attributes;
        num_contracts += other.num_contracts;

        return *this;
    }

    bool operator ==(meta_summary const& other) noexcept
    {
        return num_namespaces == other.num_namespaces &&
               num_interfaces == other.num_interfaces &&
               num_methods == other.num_methods &&
               num_properties == other.num_properties &&
               num_events == other.num_events &&
               num_fields == other.num_fields &&
               num_classes == other.num_classes &&
               num_structs == other.num_structs &&
               num_enums == other.num_enums &&
               num_delegates == other.num_delegates &&
               num_attributes == other.num_attributes &&
               num_contracts == other.num_contracts;
    }

    bool operator !=(meta_summary const& other) noexcept
    {
        return !operator ==(other);
    }
};


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

    void write_value(bool value)
    {
        write(value ? "TRUE"sv : "FALSE"sv);
    }

    void write_value(char16_t value)
    {
        write_printf("%#0hx", value);
    }

    void write_value(int8_t value)
    {
        write_printf("%hhd", value);
    }

    void write_value(uint8_t value)
    {
        write_printf("%#0hhx", value);
    }

    void write_value(int16_t value)
    {
        write_printf("%hd", value);
    }

    void write_value(uint16_t value)
    {
        write_printf("%#0hx", value);
    }

    void write_value(int32_t value)
    {
        write_printf("%d", value);
    }

    void write_value(uint32_t value)
    {
        write_printf("%#0x", value);
    }

    void write_value(int64_t value)
    {
        write_printf("%lld", value);
    }

    void write_value(uint64_t value)
    {
        write_printf("%#0llx", value);
    }

    void write_value(float value)
    {
        write_printf("%f", value);
    }

    void write_value(double value)
    {
        write_printf("%f", value);
    }

    void write_value(std::string_view value)
    {
        write("\"%\"", value);
    }

    void write(Constant const& value)
    {
        switch (value.Type()) {
        case ConstantType::Boolean:
            write_value(value.ValueBoolean());
            break;
        case ConstantType::Char:
            write_value(value.ValueChar());
            break;
        case ConstantType::Int8:
            write_value(value.ValueInt8());
            break;
        case ConstantType::UInt8:
            write_value(value.ValueUInt8());
            break;
        case ConstantType::Int16:
            write_value(value.ValueInt16());
            break;
        case ConstantType::UInt16:
            write_value(value.ValueUInt16());
            break;
        case ConstantType::Int32:
            write_value(value.ValueInt32());
            break;
        case ConstantType::UInt32:
            write_value(value.ValueUInt32());
            break;
        case ConstantType::Int64:
            write_value(value.ValueInt64());
            break;
        case ConstantType::UInt64:
            write_value(value.ValueUInt64());
            break;
        case ConstantType::Float32:
            write_value(value.ValueFloat32());
            break;
        case ConstantType::Float64:
            write_value(value.ValueFloat64());
            break;
        case ConstantType::String:
            write_value(value.ValueString());
            break;
        case ConstantType::Class:
            write("null");
            break;
        }
    }

    void write(meta_summary const& info)
    {
        write("%\n", info.title);

        if (!info.single_namespace) {
            write("Namespaces     %\n", info.num_namespaces);
        }

        write("Interfaces     %\n", info.num_interfaces);
        write("Methods        %\n", info.num_methods);
        write("Properties     %\n", info.num_properties);
        write("Events         %\n", info.num_events);
//        write("Fields         %\n", info.num_fields);
        write("Classes        %\n", info.num_classes);
        write("Structs        %\n", info.num_structs);
        write("Enums          %\n", info.num_enums);
        write("Delegates      %\n", info.num_delegates);
        write("Attributes     %\n", info.num_attributes);
        write("Contracts      %\n", info.num_contracts);
        write('\n');
    }
};


static void print_usage(writer& w)
{
    static auto printColumns = [](writer& w, std::string_view const& col1, std::string_view const& col2) {
        w.write_printf("  %-20s%s\n", col1.data(), col2.data());
    };

    static auto printOption = [](writer& w, cmd::option const& opt) {
        if (opt.desc.empty()) {
            return;
        }
        printColumns(w, w.write_temp("-% %", opt.name, opt.arg), opt.desc);
    };

    auto format = R"(
meta_info.exe [options...]

Options:

%  ^@<path>             Response file containing command line options

Where <spec> is one or more of:

  path                Path to winmd file or recursively scanned folder
  local               Local ^%WinDir^%\System32\WinMetadata folder
  sdk[+]              Current version of Windows SDK [with extensions]
  10.0.12345.0[+]     Specific version of Windows SDK [with extensions]
)";

    w.write(format, bind_each(printOption, options));
}

static void process_args(int const argc, char** argv)
{
    cmd::reader args{ argc, argv, options };

    if (!args || args.exists("help"))
        throw usage_exception{};

    settings.verbose = args.exists("verbose");
    settings.input = args.files("input", database::is_database);
    settings.group_lists = args.exists("group-lists");
    settings.extra_details = args.exists("extra-details");

    const auto list_all         = args.exists("list-all");
    settings.list_namespaces    = list_all || args.exists("list-namespaces");
    settings.list_interfaces    = list_all || args.exists("list-interfaces");
    settings.list_methods       = list_all || args.exists("list-methods");
    settings.list_properties    = list_all || args.exists("list-properties");
    settings.list_events        = list_all || args.exists("list-events");
    settings.list_fields        = list_all || args.exists("list-fields");
    settings.list_classes       = list_all || args.exists("list-classes");
    settings.list_structs       = list_all || args.exists("list-structs");
    settings.list_enums         = list_all || args.exists("list-enums");
    settings.list_delegates     = list_all || args.exists("list-delegates");
    settings.list_attributes    = list_all || args.exists("list-attributes");
    settings.list_contracts     = list_all || args.exists("list-contracts");

    for (auto&& include : args.values("include")) {
        settings.include.insert(include);
    }

    for (auto&& exclude : args.values("exclude")) {
        settings.exclude.insert(exclude);
    }

    settings.filter = { settings.include, settings.exclude };
}

static void write_summary(writer& w, cache const& c)
{
    meta_summary total_info("Total");
    meta_summary filtered_info("Filtered");

    for (auto const& [ns, members] : c.namespaces()) {
        meta_summary ns_info(ns, members);

        total_info += ns_info;

        if (!settings.filter.includes(members))
            continue;

        filtered_info += ns_info;

        if (settings.verbose) {
            w.write(ns_info);
        }
    }

    if (!settings.filter.empty() && filtered_info != total_info) {
        w.write(filtered_info);
    }

    w.write(total_info);
}

static void write_namespace_list(writer& w, cache const& c)
{
    w.write("Namespaces:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        w.write("%\n", ns);
    }

    w.write('\n');
}

static void write_type_collection(writer& w, std::string_view const& ns, std::vector<TypeDef> const& collection)
{
    if (settings.group_lists && !collection.empty()) {
        w.write("%:\n", ns);

        for (auto const& element : collection) {
            w.write("  %\n", element.TypeName());
        }
    } else {
        for (auto const& element : collection) {
            w.write("%.%\n", ns, element.TypeName());
        }
    }
}

static void write_interface_list(writer& w, cache const& c)
{
    w.write("Interfaces:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.interfaces);
    }

    w.write('\n');
}

static void write_method_list(writer& w, cache const& c)
{
    w.write("Methods:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.methods);
    }

    w.write('\n');
}

static void write_property_list(writer& w, cache const& c)
{
    w.write("Properties:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.properties);
    }

    w.write('\n');
}

static void write_event_list(writer& w, cache const& c)
{
    w.write("Events:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.events);
    }

    w.write('\n');
}

static void write_field_list(writer& w, cache const& c)
{
    w.write("Fields:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.fields);
    }

    w.write('\n');
}

static void write_class_list(writer& w, cache const& c)
{
    w.write("Classes:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.classes);
    }

    w.write('\n');
}

static void write_struct_list(writer& w, cache const& c)
{
    w.write("Structs:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.structs);
    }

    w.write('\n');
}

static void write_enum_list(writer& w, cache const& c)
{
    w.write("Enums:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        auto const& enums = members.enums;

        if (settings.group_lists && !enums.empty()) {
            w.write("%:\n", ns);

            for (auto const& type : enums) {
                w.write("  %\n", type.TypeName());

                if (settings.extra_details) {
                    for (auto const& field : type.FieldList()) {
                        if (auto const& constant = field.Constant()) {
                            w.write("    % = %\n", field.Name(), constant);
                        }
                    }
                }
            }
        } else {
            for (auto const& type : enums) {
                if (settings.extra_details) {
                    for (auto const& field : type.FieldList()) {
                        if (auto const& constant = field.Constant()) {
                            w.write("%.%.% = %\n", ns, type.TypeName(), field.Name(), constant);
                        }
                    }
                } else {
                    w.write("%.%\n", ns, type.TypeName());
                }
            }
        }
    }

    w.write('\n');
}

static void write_delegate_list(writer& w, cache const& c)
{
    w.write("Delegates:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.delegates);
    }

    w.write('\n');
}

static void write_attribute_list(writer& w, cache const& c)
{
    w.write("Attributes:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.attributes);
    }

    w.write('\n');
}

static void write_contract_list(writer& w, cache const& c)
{
    w.write("Contracts:\n");

    for (auto const& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.contracts);
    }

    w.write('\n');
}

static int run(int const argc, char** argv)
{
    int result{};
    writer w;

    try {
        const auto start = high_resolution_clock::now();

        process_args(argc, argv);

        cache c{ settings.input };


        if (settings.verbose) {
            std::for_each(c.databases().begin(), c.databases().end(), [&](auto&& db)
                {
                    w.write("in: %\n", db.path());
                });
        }

        w.flush_to_console();

        if (settings.list_namespaces)
            write_namespace_list(w, c);

        if (settings.list_interfaces)
            write_interface_list(w, c);

        if (settings.list_methods)
            write_method_list(w, c);

        if (settings.list_properties)
            write_property_list(w, c);

        if (settings.list_events)
            write_event_list(w, c);

        if (settings.list_fields)
            write_field_list(w, c);

        if (settings.list_classes)
            write_class_list(w, c);

        if (settings.list_structs)
            write_struct_list(w, c);

        if (settings.list_enums)
            write_enum_list(w, c);

        if (settings.list_delegates)
            write_delegate_list(w, c);

        if (settings.list_attributes)
            write_attribute_list(w, c);

        if (settings.list_contracts)
            write_contract_list(w, c);

        write_summary(w, c);

        if (settings.verbose)
            w.write("time: %ms\n", duration_cast<duration<int64_t, std::milli>>(high_resolution_clock::now() - start).count());
    }
    catch (std::exception const& e) {
        w.write("\nERROR: %\n", e.what());

        result = 1;
    }
    catch (usage_exception const&) {
        print_usage(w);
    }

    w.flush_to_console();

    return result;
}

}


int main(int const argc, char** argv)
{
    return meta_info::run(argc, argv);
}
