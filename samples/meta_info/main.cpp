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
    meta_summary(const std::string_view& title) noexcept : title{ title } {}
    meta_summary(const std::string_view& ns, const cache::namespace_members& members) noexcept : title{ ns }, single_namespace{ true }
    {
        num_namespaces = 1;
        num_interfaces = members.interfaces.size();
        num_classes = members.classes.size();
        num_structs = members.structs.size();
        num_enums = members.enums.size();
        num_delegates = members.delegates.size();
        num_attributes = members.attributes.size();
        num_contracts = members.contracts.size();

        for (const auto& iface : members.interfaces)
        {
            const auto& methods = iface.MethodList();
            const auto& properties = iface.PropertyList();
            const auto& events = iface.EventList();
            const auto& fields = iface.FieldList();

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

    meta_summary& operator +=(const meta_summary& other) noexcept
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

    bool operator ==(const meta_summary& other) noexcept
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

    bool operator !=(const meta_summary& other) noexcept
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

    std::vector<std::pair<GenericParam, GenericParam>> generic_param_stack;

    struct generic_param_guard
    {
        explicit generic_param_guard(writer* arg) : owner(arg)
        {}

        ~generic_param_guard()
        {
            if (owner) {
                owner->generic_param_stack.pop_back();
            }
        }

        generic_param_guard(generic_param_guard&& other) : owner(other.owner)
        {
            owner = nullptr;
        }
        generic_param_guard& operator=(const generic_param_guard&) = delete;
        writer* owner;
    };

    generic_param_guard push_generic_params(std::pair<GenericParam, GenericParam>&& arg)
    {
        generic_param_stack.push_back(std::move(arg));
        return generic_param_guard{ this };
    }

    void write_indent()
    {
        for (int32_t i = 0; i < indent; i++) {
            writer_base::write_impl("    ");
        }
    }

    void write_impl(const std::string_view& value)
    {
        std::string_view::size_type current_pos{ 0 };
        auto on_new_line = back() == '\n';

        while (true) {
            const auto pos = value.find('\n', current_pos);

            if (pos == std::string_view::npos) {
                if (current_pos < value.size()) {
                    if (on_new_line) {
                        write_indent();
                    }

                    writer_base::write_impl(value.substr(current_pos));
                }

                return;
            }

            auto current_line = value.substr(current_pos, pos - current_pos + 1);
            auto empty_line = current_line[0] == '\n';

            if (on_new_line && !empty_line) {
                write_indent();
            }

            writer_base::write_impl(current_line);

            on_new_line = true;
            current_pos = pos + 1;
        }
    }

    void write_impl(const char value)
    {
        if (back() == '\n' && value != '\n') {
            write_indent();
        }

        writer_base::write_impl(value);
    }

    template <typename... Args>
    std::string write_temp(const std::string_view& value, const Args& ... args)
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

    void write(const Constant& value)
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

    void write(const TypeDef& type)
    {
        write("%.%", type.TypeNamespace(), type.TypeName());
    }

    void write(const TypeRef& type)
    {
        auto ns = type.TypeNamespace();

        if (ns == current) {        // DJ: TODO: Update !!!
            write("%", type.TypeName());
        } else {
            write("%.%", type.TypeNamespace(), type.TypeName());
        }
    }

    void write(const TypeSpec& type)
    {
        write(type.Signature().GenericTypeInst());
    }

    void write(const coded_index<TypeDefOrRef>& type)
    {
        switch (type.type()) {
        case TypeDefOrRef::TypeDef:
            write(type.TypeDef());
            break;

        case TypeDefOrRef::TypeRef:
            write(type.TypeRef());
            break;

        case TypeDefOrRef::TypeSpec:
            write(type.TypeSpec());
            break;
        }
    }

    void write(const GenericTypeInstSig& type)
    {
        write("%<%>",
            type.GenericType(),
            bind_list(", ", type.GenericArgs()));
    }

    void write(TypeSig const& signature)
    {
        std::visit(overloaded
            {
                [&](ElementType type) {
                    if (type <= ElementType::String)
                    {
                        static constexpr const char* primitives[]
                        {
                            "End",
                            "Void",
                            "Boolean",
                            "Char",
                            "Int8",
                            "UInt8",
                            "Int16",
                            "UInt16",
                            "Int32",
                            "UInt32",
                            "Int64",
                            "UInt64",
                            "Single",
                            "Double",
                            "String"
                        };

                        write(primitives[static_cast<uint32_t>(type)]);
                    } else if (type == ElementType::Object)
                    {
                        write("Object");
                    }
                },
                [&](GenericTypeIndex var) {
                    write("%", begin(generic_param_stack.back())[var.index].Name());
                },
                [&](GenericMethodTypeIndex) {
                    throw_invalid("Generic methods not supported.");
                },
                [&](auto&& type) {
                    write(type);
                }
            },
            signature.Type());
    }

    void write(InterfaceImpl const& impl)
    {
        write(impl.Interface());
    }

    void write(MethodDef const& method)
    {
        auto signature{ method.Signature() };

        auto param_list = method.ParamList();
        Param param;

        if (method.Signature().ReturnType() && !empty(param_list) && param_list.first.Sequence() == 0)
        {
            param = param_list.first + 1;
        } else
        {
            param = param_list.first;
        }

        bool first{ true };

        for (auto&& arg : signature.Params())
        {
            if (first)
            {
                first = false;
            } else
            {
                write(", ");
            }

            if (arg.ByRef())
            {
                write("ref ");
            }

            if (is_const(arg))
            {
                write("const ");
            }

            write("% %", arg.Type(), param.Name());
            ++param;
        }
    }

    void write(RetTypeSig const& signature)
    {
        if (signature)
        {
            write(signature.Type());
        } else
        {
            write("void");
        }
    }

    std::vector<Field> find_enumerators(ElemSig::EnumValue const& arg)
    {
        std::vector<Field> result;
        uint64_t const original_value = std::visit([](auto&& value) { return static_cast<uint64_t>(value); }, arg.value);
        uint64_t flags_value = original_value;

        auto get_enumerator_value = [](auto&& arg) -> uint64_t
        {
            if constexpr (std::is_integral_v<std::remove_reference_t<decltype(arg)>>)
            {
                return static_cast<uint64_t>(arg);
            } else
            {
                throw_invalid("Non-integral enumerator encountered");
            }
        };

        for (auto const& field : arg.type.m_typedef.FieldList())
        {
            if (auto const& constant = field.Constant())
            {
                uint64_t const enumerator_value = std::visit(get_enumerator_value, constant.Value());
                if (enumerator_value == original_value)
                {
                    result.assign(1, field);
                    return result;
                } else if ((flags_value & enumerator_value) == enumerator_value)
                {
                    result.push_back(field);
                    flags_value &= (~enumerator_value);
                }
            }
        }

        // Didn't find a match, or a set of flags that could build up the value
        if (flags_value != 0)
        {
            result.clear();
        }
        return result;
    }

    void write(const FixedArgSig& arg)
    {
        std::visit(overloaded{
            [this](ElemSig::SystemType arg) {
                write(arg.name);
            },
            [this](ElemSig::EnumValue arg) {
                const auto enumerators = find_enumerators(arg);
                if (enumerators.empty()) {
                    std::visit([this](auto&& value) { write_value(value); }, arg.value);
                } else {
                    bool first = true;
                    for (auto const& enumerator : enumerators)
                    {
                        if (!first)
                        {
                            write(" | ");
                        }
                        write("%.%.%", arg.type.m_typedef.TypeNamespace(), arg.type.m_typedef.TypeName(), enumerator.Name());
                        first = false;
                    }
                }
            },
            [this](auto&& arg) {
                write_value(arg);
            }
            }, std::get<ElemSig>(arg.value).value);
    }

    void write(const NamedArgSig& arg)
    {
        write(arg.value);
    }

    void write(const meta_summary& info)
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
    static auto printColumns = [](writer& w, const std::string_view& col1, const std::string_view& col2) {
        w.write_printf("  %-20s%s\n", col1.data(), col2.data());
    };

    static auto printOption = [](writer& w, const cmd::option& opt) {
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

static void process_args(int argc, char** argv)
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

static void write_summary(writer& w, const cache& c)
{
    meta_summary total_info("Total");
    meta_summary filtered_info("Filtered");

    for (const auto& [ns, members] : c.namespaces()) {
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

static void write_namespace_list(writer& w, const cache& c)
{
    w.write("Namespaces:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        w.write("%\n", ns);
    }

    w.write('\n');
}

static void write_type_collection(writer& w, const std::string_view& ns, const std::vector<TypeDef>& collection)
{
    if (settings.group_lists && !collection.empty()) {
        w.write("%:\n", ns);

        for (const auto& element : collection) {
            w.write("  %\n", element.TypeName());
        }
    } else {
        for (const auto& element : collection) {
            w.write("%.%\n", ns, element.TypeName());
        }
    }
}

static void write_interface_list(writer& w, const cache& c)
{
    w.write("Interfaces:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.interfaces);
    }

    w.write('\n');
}

static void write_method_list(writer& w, const cache& c)
{
    w.write("Methods:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.methods);
    }

    w.write('\n');
}

static void write_property_list(writer& w, const cache& c)
{
    w.write("Properties:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.properties);
    }

    w.write('\n');
}

static void write_event_list(writer& w, const cache& c)
{
    w.write("Events:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.events);
    }

    w.write('\n');
}

static void write_field_list(writer& w, const cache& c)
{
    w.write("Fields:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

//        write_type_collection(w, ns, members.fields);
    }

    w.write('\n');
}

static void write_class_list(writer& w, const cache& c)
{
    w.write("Classes:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.classes);
    }

    w.write('\n');
}

static void write_struct_list(writer& w, const cache& c)
{
    w.write("Structs:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        const auto& structs = members.structs;

        if (settings.group_lists && !structs.empty()) {
            w.write("%:\n", ns);

            for (const auto& type : structs) {
                w.write("  %\n", type.TypeName());

                if (settings.extra_details) {
                    for (const auto& field : type.FieldList()) {
                        w.write("    % (%)\n", field.Name(), field.Signature().Type());
                    }
                }
            }
        } else {
            for (const auto& type : structs) {
                if (settings.extra_details) {
                    for (const auto& field : type.FieldList()) {
                        w.write("%.%.% (%)\n", ns, type.TypeName(), field.Name(), field.Signature().Type());
                    }
                } else {
                    w.write("%.%\n", ns, type.TypeName());
                }
            }
        }
    }

    w.write('\n');
}

static void write_enum_list(writer& w, const cache& c)
{
    w.write("Enums:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        const auto& enums = members.enums;

        if (settings.group_lists && !enums.empty()) {
            w.write("%:\n", ns);

            for (const auto& type : enums) {
                w.write("  %\n", type.TypeName());

                if (settings.extra_details) {
                    for (const auto& field : type.FieldList()) {
                        if (const auto& constant = field.Constant()) {
                            w.write("    % = %\n", field.Name(), constant);
                        }
                    }
                }
            }
        } else {
            for (const auto& type : enums) {
                if (settings.extra_details) {
                    for (const auto& field : type.FieldList()) {
                        if (const auto& constant = field.Constant()) {
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

static void write_delegate_list(writer& w, const cache& c)
{
    w.write("Delegates:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.delegates);
    }

    w.write('\n');
}

static void write_attribute_list(writer& w, const cache& c)
{
    w.write("Attributes:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.attributes);
    }

    w.write('\n');
}

static void write_contract_list(writer& w, const cache& c)
{
    w.write("Contracts:\n");

    for (const auto& [ns, members] : c.namespaces()) {
        if (!settings.filter.includes(members))
            continue;

        write_type_collection(w, ns, members.contracts);
    }

    w.write('\n');
}

static int run(int argc, char** argv)
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
    catch (const std::exception& e) {
        w.write("\nERROR: %\n", e.what());

        result = 1;
    }
    catch (const usage_exception&) {
        print_usage(w);
    }

    w.flush_to_console();

    return result;
}

}


int main(int argc, char** argv)
{
    return meta_info::run(argc, argv);
}
