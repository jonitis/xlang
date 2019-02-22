#include "pch.h"
#include "settings.h"
#include "writer.h"

namespace rgm
{
    using namespace xlang;
    using namespace xlang::text;
    using namespace xlang::meta::reader;

    settings_type settings;   

    struct usage_exception {};

    static constexpr cmd::option options[]
    {
        { "input", 0, cmd::option::no_max, "<path>", "Windows metadata to include in projection" },
        { "reference", 0, cmd::option::no_max, "<path>", "Windows metadata to reference from projection" },
    };

    void process_args(int const argc, char** argv)
    {
        cmd::reader args{ argc, argv, options };

        if (!args)
        {
            throw usage_exception{};
        }

        settings.input = args.files("input", database::is_database);
        settings.reference = args.files("reference", database::is_database);
    }

    static void print_usage(writer& w)
    {
        static auto printColumns = [](writer& w, std::string_view const& col1, std::string_view const& col2)
        {
            w.write_printf("  %-20s%s\n", col1.data(), col2.data());
        };

        static auto printOption = [](writer& w, cmd::option const& opt)
        {
            if(opt.desc.empty())
            {
                return;
            }
            printColumns(w, w.write_temp("-% %", opt.name, opt.arg), opt.desc);
        };

        auto format = R"(
RGM/xlang v%
Copyright (c) Microsoft Corporation. All rights reserved.

  rgm.exe [options...]

Options:

%
)";
        w.write(format, "0.0.1", bind_each(printOption, options));
    }

    static auto get_files_to_cache()
    {
        std::vector<std::string> files;
        files.insert(files.end(), settings.input.begin(), settings.input.end());
        files.insert(files.end(), settings.reference.begin(), settings.reference.end());
        return files;
    }

    static void write_if_true(writer& w, bool value, std::string_view text)
    {
        if (value)
        {
            w.write(text);
        }
    }

	void write_enum_constant(writer& w, Constant const& constant)
	{
		if (constant)
		{
			w.write(" = %", constant);
		}

	}
    static void write_enum(writer& w, TypeDef const& type)
    {
        w.write(".class % % % %%%.% extends [mscorlib]System.Enum\n{\n", 
			type.Flags().Visibility(),
			type.Flags().Layout(),
			type.Flags().StringFormat(),
			bind<write_if_true>(type.Flags().WindowsRuntime(), "windowsruntime "),
			bind<write_if_true>(type.Flags().Sealed(), "sealed "),
			type.TypeNamespace(),
			type.TypeName());

        for(auto&& field : type.FieldList())
        {
			w.write("    .field % %%%%% %%",
				field.Flags().Access(),
				bind<write_if_true>(field.Flags().Static(), "static "),
				bind<write_if_true>(field.Flags().SpecialName(), "specialname "),
				bind<write_if_true>(field.Flags().RTSpecialName(), "rtspecialname "),
				bind<write_if_true>(field.Flags().Literal(), "literal valuetype "),
				field.Signature().Type(),
                field.Name(),
				bind<write_enum_constant>(field.Constant()));
            w.write("\n");
        }
        w.write("}\n\n");
    }

    static void run(int const argc, char** argv)
    {
        writer w;

        try
        {
            process_args(argc, argv);
            cache c{ get_files_to_cache() };

            for (auto&&[ns, members] : c.namespaces())
            { 
                if (ns.compare(0, 18, "Windows.Foundation") == 0)
                    continue;

                w.write_each<write_enum>(members.enums);
            }
        }
        catch (usage_exception const&)
        {
            print_usage(w);
        }
        catch (std::exception const& e)
        {
            w.write(" error: %\n", e.what());
        }

        w.flush_to_console();
		system("pause");
    }
}

int main(int const argc, char** argv)
{
    return rgm::run(argc, argv);
}