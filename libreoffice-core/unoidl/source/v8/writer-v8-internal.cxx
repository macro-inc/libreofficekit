#include "writer.hxx"
#include <iostream>

namespace writer {

namespace {
OUString translateNamespace(OUString const& name) {
    return "::" + name.replaceFirst("com.sun.star.", "css.").replaceAll(".", "::");
}
}

void V8WriterInternal::writeName(OUString const& name) { out(translateNamespace(name)); }

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
                                             bool isRef, std::unordered_set<int>& declared) {
    auto cpp_class = translateNamespace(name);
    for (auto& m : entity->getDirectMethods()) {
        if (shouldSkipMethod(m)
            || declared.find(name.hashCode() ^ m.name.hashCode()) != declared.end())
            continue;
        declared.emplace(name.hashCode() ^ m.name.hashCode());
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
        auto returnType = resolveTypedef(m.returnType);
        bool isVoid = returnType == "void";
        auto entity__ = entities_.find(returnType);
        bool isInterface
            = !isVoid && entity__ != entities_.end()
              && entity__->second->entity->getSort() == unoidl::Entity::SORT_INTERFACE_TYPE;
        out("*error = nullptr;\n");

        out("try {\n");
        if (isInterface) {
            // compiler will optimize out the result pointer with auto type and cause errors without this
            out("::css::uno::Reference<" + translateNamespace(returnType) + "> tmp_result = ");
        } else if (!isVoid) {
            out("auto tmp_result = ");
        }
        out("static_cast<" + cpp_class + "*>(this_)->" + m.name + "(");
        for (auto& a : m.parameters) {
            writeCToCpp(a.type, a.name);
            if (&a != &m.parameters.back())
                out(", ");
        }
        out(");\n");
        if (returnType == "any") {
            out(R"(
                uno_type_any_construct(result, tmp_result.pData, tmp_result.pType, ::css::uno::cpp_acquire);
            )");
        } else if (!isVoid) {
            // strings need to be acquired before they are returned on stack
            if (returnType == "string") {
                out("rtl_uString_acquire(tmp_result.pData);\n");
            }
            // interfaces passed by uno::Reference need to be acquired before they are returned on stack
            if (isInterface) {
                out("static_cast<::css::uno::XInterface*>(static_cast<void*>(tmp_result.get()))->acquire();\n");
            }
            out("*result = ");
            writeCppToC(returnType, "tmp_result");
            out(";\n");
        }
        out("} catch (const ::css::uno::Exception& exception_) {\n");
        out("rtl_uString_acquire(exception_.Message.pData);\n");
        out("*error = exception_.Message.pData;\n");
        out("}\n");

        out("}\n");
    }
    for (auto& i : entity->getDirectMandatoryBases()) {
        // acquire/release are handled on object create/destroy
        if (i.name == "com.sun.star.uno.XInterface")
            continue;

        auto* base_ent = static_cast<unoidl::InterfaceTypeEntity*>(entities_[i.name]->entity.get());
        writeInterfaceMethods(i.name, base_ent, isRef, declared);
    }
}

void V8WriterInternal::writeInterfaceMethodsInit(OUString const& name,
                                                 rtl::Reference<unoidl::InterfaceTypeEntity> entity,
                                                 bool isRef, std::unordered_set<int>& declared) {
    for (const auto& m : entity->getDirectMethods()) {
        if (shouldSkipMethod(m)
            || declared.find(name.hashCode() ^ m.name.hashCode()) != declared.end())
            continue;
        declared.emplace(name.hashCode() ^ m.name.hashCode());
        out(cName(name) + "_" + m.name + ",\n");
    }
    for (auto& i : entity->getDirectMandatoryBases()) {
        // acquire/release are handled on object create/destroy
        if (i.name == "com.sun.star.uno.XInterface")
            continue;

        auto* base_ent = static_cast<unoidl::InterfaceTypeEntity*>(entities_[i.name]->entity.get());
        writeInterfaceMethodsInit(i.name, base_ent, isRef, declared);
    }
}

namespace {
OUString translateSimpleTypeCpp(OUString const& name) {
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
        return "::rtl::OUString";
    if (name == "type")
        return "::css::uno::Type";
    if (name == "any")
        return "::css::uno::Any";
    return name;
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
            out(translateSimpleTypeCpp(nucl));
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
        return out("OUString(" + name + ")");
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

inline typelib_TypeDescriptionReference* interfaceTypeFromFQN(const char *str, int len) {
    switch (hash(std::string_view(str, len))) {
)");

    for (auto& i : interfaces) {
        out("case hash(\"" + i + "\"): ");
        out("return ::cppu::UnoType<" + translateNamespace(i) + ">::get().getTypeLibType();\n");
    }

    out(R"(
    }
    return nullptr;
}

inline typelib_TypeDescriptionReference* structTypeFromFQN(const char *str, int len) {
    switch (hash(std::string_view(str, len))) {
)");

    for (auto& i : interfaces) {
        out("case hash(\"" + i + "\"): ");
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

    std::unordered_set<int> declared{};
    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());
        writeInterfaceMethods(i, entity, false, declared);
    }
    out("inline void _init(struct _UnoV8Methods* methods) {\n"
        "*methods = {\n");
    declared.clear();
    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());
        writeInterfaceMethodsInit(i, entity, false, declared);
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
    out(R"(/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
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
    std::unordered_set<int> declared{};
    for (const auto& i : interfaces) {
        std::map<OUString, writer::Entity*>::iterator j(entities_.find(i));
        auto* entity = static_cast<unoidl::InterfaceTypeEntity*>(j->second->entity.get());
        writeInterfaceMethods(i, entity, true, declared);
    }
    out("\n};\n");

    out(R"(
#endif
)");
    close();
}
}
