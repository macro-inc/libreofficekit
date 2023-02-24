#include "writer.hxx"

namespace writer {

namespace {
OUString translateNamespace(OUString const& name) {
    return "::" + name.replaceFirst("com.sun.star.", "css.").replaceAll(".", "::");
}
}

void V8WriterInternal::writeName(OUString const& name) { out(translateNamespace(name)); }

OUString V8WriterInternal::translateSimpleType(OUString const& name) {
    if (name == "void")
        return "void";
    if (name == "boolean")
        return "sal_Bool";
    if (name == "byte")
        return "sal_Int8";
    if (name == "short")
        return "sal_Int16";
    if (name == "unsigned short")
        return "sal_uInt16";
    if (name == "long")
        return "sal_Int32";
    if (name == "unsigned long")
        return "sal_uInt32";
    if (name == "hyper")
        return "sal_Int64";
    if (name == "unsigned hyper")
        return "sal_uInt64";
    if (name == "float")
        return "float";
    if (name == "double")
        return "double";
    if (name == "char")
        return "sal_Unicode";
    if (name == "string")
        return "rtl_uString*";
    if (name == "type")
        return "typelib_TypeDescriptionReference*";
    if (name == "any")
        return "uno_Any";

    return name;
}

void V8WriterInternal::writeType(OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool isEntity;
    OUString nucl(decomposeType(name, &rank, &args, &isEntity));

    if (rank > 0) {
        return out("uno_Sequence*");
    }
    if (!isEntity) {
        return out(translateSimpleType(nucl));
    }
    auto entity = entities_.find(nucl);
    if (entity == entities_.end()) {
        return out(nucl);
    }
    auto sort = entity->second->entity->getSort();
    switch (sort) {
        case unoidl::Entity::SORT_INTERFACE_TYPE:
            return out("void*");
        case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
            return out("struct " + cStructName(nucl));
        case unoidl::Entity::SORT_TYPEDEF:
            return writeType(
                static_cast<unoidl::TypedefEntity*>(entity->second->entity.get())->getType());
        case unoidl::Entity::SORT_ENUM_TYPE:
            return out("unsigned int");
        default:
            break;
    }
}

void V8WriterInternal::writeMethodParams(const unoidl::InterfaceTypeEntity::Method& method) {
    out("void* this_");
    if (method.returnType != "void") {
        out(", ");
        writeType(method.returnType);
        out("* result");
    }
    out(", rtl_uString **error");
    for (auto& a : method.parameters) {
        out(", ");
        writeType(a.type);
        out(" " + a.name);
    }
}

void V8WriterInternal::writeInterfaceMethods(OUString const& name,
                                             rtl::Reference<unoidl::InterfaceTypeEntity> entity,
                                             bool isRef) {
    auto cpp_class = translateNamespace(name);
    for (auto& m : entity->getDirectMethods()) {
        if (shouldSkipMethod(m))
            continue;
        if (!isRef)
            out("inline ");
        out("void ");
        if (isRef)
            out("(*");
        out(cName(name) + "_" + m.name);
        if (isRef)
            out(")");
        out("(");
        writeMethodParams(m);
        out(")");

        if (isRef) {
            out(";\n");
            continue;
        }
        out("{\n");
        bool isVoid = m.returnType == "void";
        out("*error = nullptr;\n");

        out("try {\n");
        if (!isVoid) {
            out("auto tmp_result = ");
        }
        out("static_cast<" + cpp_class + "*>(this_)->" + m.name + "(");
        for (auto& a : m.parameters) {
            writeCToCpp(a.type, a.name);
            if (&a != &m.parameters.back())
                out(", ");
        }
        out(");\n");
        if (!isVoid) {
            out("*result = ");
            writeCppToC(m.returnType, "tmp_result");
            out(";\n");
        }
        out("} catch (const ::css::uno::Exception& e) {\n");
        out("*error = e.Message.pData;\n");
        out("}\n");

        out("}\n");
    }
}

void V8WriterInternal::writeCToCpp(OUString const& type, OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool isEntity;
    OUString nucl(decomposeType(type, &rank, &args, &isEntity));

    if (rank > 0) {
        for (std::size_t i = 0; i != rank; ++i) {
            out("::css::uno::Sequence<");
        }
        if (isEntity) {
            auto entity = entities_.find(nucl);
            if (entity == entities_.end()) {
                out(translateNamespace(nucl));
            }
            auto sort = entity->second->entity->getSort();
            switch (sort) {
                case unoidl::Entity::SORT_INTERFACE_TYPE:
                    out("::css::uno::Reference<" + translateNamespace(nucl) + ">");
                    break;
                case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
                case unoidl::Entity::SORT_TYPEDEF:
                case unoidl::Entity::SORT_ENUM_TYPE:
                    out(translateNamespace(nucl));
                    break;
                default:
                    break;
            }
        } else {
            out(V8Writer::translateSimpleType(nucl));
        }

        for (std::size_t i = 0; i != rank; ++i) {
            out(">");
        }
        out("(" + name + ", SAL_NO_ACQUIRE)");
        return;
    }

    if (isEntity) {
        auto entity = entities_.find(nucl);
        if (entity == entities_.end()) {
            return out(nucl);
        }
        auto sort = entity->second->entity->getSort();
        switch (sort) {
            case unoidl::Entity::SORT_INTERFACE_TYPE:
                return out("::css::uno::Reference<" + translateNamespace(nucl) + ">(static_cast<"
                           + translateNamespace(nucl) + "*>(" + name + "), SAL_NO_ACQUIRE)");
            case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
                return out("::unov8::c_struct_to_cpp::" + cName(nucl) + "(" + name + ")");
            case unoidl::Entity::SORT_TYPEDEF:
                return writeCToCpp(
                    static_cast<unoidl::TypedefEntity*>(entity->second->entity.get())->getType(),
                    name);
            case unoidl::Entity::SORT_ENUM_TYPE:
                return out("(" + translateNamespace(nucl) + ")" + name);
            default:
                break;
        }
        return;
    }

    if (nucl == "string") {
        return out("OUString(" + name + ", SAL_NO_ACQUIRE)");
    } else if (nucl == "type") {
        return out("*reinterpret_cast<::css::uno::Type*>(&" + name + ")");
    } else if (nucl == "any") {
        return out("*reinterpret_cast<::css::uno::Any*>(&" + name + ")");
    }

    return out(name);
}

void V8WriterInternal::writeCppToC(OUString const& type, OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool isEntity;
    OUString nucl(decomposeType(type, &rank, &args, &isEntity));

    if (rank > 0) {
        return out(name + ".get()");
    }

    if (isEntity) {
        auto entity = entities_.find(nucl);
        if (entity == entities_.end()) {
            return out(nucl);
        }
        auto sort = entity->second->entity->getSort();
        switch (sort) {
            case unoidl::Entity::SORT_INTERFACE_TYPE:
                return out("static_cast<void*>(" + name + ".get())");
            case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
                return out("::unov8::cpp_struct_to_c::" + cName(nucl) + "(" + name + ")");
            case unoidl::Entity::SORT_TYPEDEF:
                return writeCppToC(
                    static_cast<unoidl::TypedefEntity*>(entity->second->entity.get())->getType(),
                    name);
            case unoidl::Entity::SORT_ENUM_TYPE:
                return out("(unsigned int)" + name);
            default:
                break;
        }
        return;
    }

    if (nucl == "string") {
        return out(name + ".pData");
    } else if (nucl == "type") {
        return out(name + ".getTypeLibType()");
    } else if (nucl == "any") {
        return out("*reinterpret_cast<uno_Any*>(&" + name + ")");
    }

    return out(name);
}

void V8WriterInternal::writeCStructToCpp(OUString const& name) {
    auto i = entities_.find(name);
    if (i == entities_.end())
        return;
    auto* entity = static_cast<unoidl::PlainStructTypeEntity*>(i->second->entity.get());
    auto cpp_struct = translateNamespace(name);
    out("inline " + cpp_struct + " " + cName(name) + "(struct " + cStructName(name)
        + " c_struct) {\n");
    out(cpp_struct + " result;\n");

    // re-using the c-struct to cpp-struct method for the base should reduce complexity in the generator code
    auto base = entity->getDirectBase();
    if (!base.isEmpty()) {
        auto base_method = cName(base);
        out("(" + translateNamespace(base) + "&)result = " + base_method + "(c_struct.base);\n");
    }
    for (auto& m : entity->getDirectMembers()) {
        out("result." + m.name + " = ");
        writeCToCpp(m.type, "c_struct." + m.name);
        out(";\n");
    }
    out("return result;\n");

    out("}\n");
}

void V8WriterInternal::writeCppStructToC(OUString const& name) {
    auto i = entities_.find(name);
    if (i == entities_.end())
        return;
    auto* entity = static_cast<unoidl::PlainStructTypeEntity*>(i->second->entity.get());
    auto cpp_struct = translateNamespace(name);
    OUString c_struct = "struct " + cStructName(name);
    out("inline " + c_struct + " " + cName(name) + "(" + cpp_struct + " cpp_struct) {\n");
    out(c_struct + " result;\n");

    auto base = entity->getDirectBase();
    if (!base.isEmpty()) {
        auto base_method = cName(base);
        out("result.base = " + base_method + "(cpp_struct);\n");
    }
    for (auto& m : entity->getDirectMembers()) {
        out("result." + m.name + " = ");
        writeCppToC(m.type, "cpp_struct." + m.name);
        out(";\n");
    }
    out("return result;\n");

    out("}\n");
}

void V8WriterInternal::writeInternalHeader() {
    createEntityFile("unov8", ".hxx");
    out(R"(
#ifndef UNOV8_INTERNAL_HXX_
#define UNOV8_INTERNAL_HXX_
#include <cppu/unotype.hxx>
#include "unov8.h"
)");

    std::vector<OUString> interfaces;
    std::vector<OUString> structs;
    std::vector<OUString> enums;
    for (const auto& i : sorted_) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        // skip irrelevant entities and those without bindings in JS/C++
        if (j == entities_.end() || !j->second->relevant)
            continue;

        switch (j->second->entity->getSort()) {
            case unoidl::Entity::SORT_INTERFACE_TYPE:
                interfaces.emplace_back(j->first);
                break;
            case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
                structs.emplace_back(j->first);
                break;
            case unoidl::Entity::SORT_ENUM_TYPE:
                enums.emplace_back(j->first);
                break;
            default:
                continue;
        }

        out("#include <" + i.replaceAll(".", "/") + ".hpp>\n");
    }

    out(R"(
namespace unov8 {

// djb2 hash
constexpr uint32_t hash(const std::string_view key) {
    uint32_t hash = 5381;
    for (const auto& e : key)
        hash = ((hash << 5) + hash) + e;
    return hash;
}

inline typelib_TypeDescriptionReference* interfaceType(uint32_t type_hash) {
    switch (type_hash) {
)");

    for (auto& i : interfaces) {
        out("case hash(\"" + simplifyNamespace(i) + "\"): ");
        out("return ::cppu::UnoType<" + translateNamespace(i) + ">::get().getTypeLibType();\n");
    }

    out(R"(
    }
    return nullptr;
}

inline typelib_TypeDescriptionReference* interfaceType(const char *str, int len) {
    return interfaceType(hash(std::string_view(str, len)));
}

void* as(void* interface, const char* type_name, int type_name_len) {
    if (!interface) return nullptr;
    auto* type_desc = interfaceType(type_name, type_name_len);
    if (!type_desc) return nullptr;
    return ::css::uno::cpp_queryInterface(interface, type_desc);
}

inline typelib_TypeDescriptionReference* enumType(uint32_t type_hash) {
    switch (type_hash) {
)");

    for (auto& i : enums) {
        out("case hash(\"" + simplifyNamespace(i) + "\"): ");
        out("return ::cppu::UnoType<" + translateNamespace(i) + ">::get().getTypeLibType();\n");
    }

    out(R"(
    }
    return nullptr;
}

inline typelib_TypeDescriptionReference* structType(uint32_t type_hash) {
    switch (type_hash) {
)");

    for (auto& i : structs) {
        out("case hash(\"" + simplifyNamespace(i) + "\"): ");
        out("return ::cppu::UnoType<" + translateNamespace(i) + ">::get().getTypeLibType();\n");
    }

    out(R"(
    }
    return nullptr;
}

namespace c_struct_to_cpp {
)");

    for (auto& i : structs) {
        writeCStructToCpp(i);
    }

    out(R"(
} // namespace c_struct_to_cpp

namespace cpp_struct_to_c {
)");

    for (auto& i : structs) {
        writeCppStructToC(i);
    }

    out(R"(
} // namespace cpp_struct_to_c

namespace methods {
)");

    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());
        writeInterfaceMethods(i, entity, false);
    }
    out("inline void _init(struct _UnoV8Methods* methods) {\n"
        "*methods = {\n");
    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());

        for (const auto& m : entity->getDirectMethods()) {
            if (shouldSkipMethod(m))
                continue;
            out("." + cName(i) + "_" + m.name + " = " + cName(i) + "_" + m.name + ",\n");
        }
    }
    out("};\n"
        "}\n");

    out(R"(
} // namespace methods

} // namespace unov8
#endif
)");
    close();
}

void V8WriterInternal::writeSharedHeader() {
    createEntityFile("unov8", ".h");
    out(R"(
#ifndef UNOV8_H_
#define UNOV8_H_
#include <sal/config.h>
#include <sal/types.h>
#include <rtl/ustring.h>
#include <typelib/typedescription.h>
#include <uno/any2.h>
#include <uno/sequence2.h>
)");

    std::vector<OUString> interfaces;
    std::vector<OUString> structs;
    for (const auto& i : sorted_) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        // skip irrelevant entities and those without bindings in JS/C++
        if (j == entities_.end() || !j->second->relevant)
            continue;

        switch (j->second->entity->getSort()) {
            case unoidl::Entity::SORT_INTERFACE_TYPE:
                interfaces.emplace_back(j->first);
                break;
            case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE:
                structs.emplace_back(j->first);
                break;
            default:
                continue;
        }

        out("#include <" + i.replaceAll(".", "/") + ".hpp>\n");
    }

    for (const auto& i : structs) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::PlainStructTypeEntity*>(j->second->entity.get());
        out("struct " + cStructName(i) + " {\n");
        auto base = entity->getDirectBase();
        if (!base.isEmpty()) {
            out("struct " + cStructName(base) + " base;\n");
        }
        for (auto& m : entity->getDirectMembers()) {
            writeType(m.type);
            out(" " + m.name + ";\n");
        }
        out("};\n");
    }

    out("struct _UnoV8Methods {\n");
    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());
        writeInterfaceMethods(i, entity, true);
    }
    out("\n};\n");

    out(R"(
#endif
)");
    close();
}
}
