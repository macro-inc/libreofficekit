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
        return "::rtl::OUString";
    if (name == "type")
        return "::css::uno::Type";
    if (name == "any")
        return "::css::uno::Any";

    return name;
}

void V8WriterInternal::writeType(OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool entity;
    OUString nucl(decomposeType(name, &rank, &args, &entity));

    for (std::size_t i = 0; i != rank; ++i) {
        out("::css::uno::Sequence<");
    }
    if (entity) {
        writeName(nucl);
        out("&");
    } else {
        out(translateSimpleType(nucl));
    }

    if (!args.empty()) {
        out("<");
        for (auto i(args.begin()); i != args.end(); ++i) {
            if (i != args.begin()) {
                out(", ");
            }
            writeType(*i);
        }
        out(">");
    }
    for (std::size_t i = 0; i != rank; ++i) {
        out(">");
    }
}

void V8WriterInternal::writeInterfaceDependency(OUString const& dependentName,
                                                OUString const& dependencyName, bool published) {
    // adjacent interfaces can cause to a self-referencing import that should be avoided
    if (dependencyName == dependentName)
        return;

    // calculate the relative path for the dependency from the dependent
    OUString dependent_ = simplifyNamespace(dependentName);
    OUString dependency_ = simplifyNamespace(dependencyName);

    sal_Int32 i = 0, j = 0;
    OUString dependencyNS;
    // skip namespace components that are the same
    for (;;) {
        OUString dependentNS(dependent_.getToken(0, '.', j));
        if (j == -1)
            break;
        dependencyNS = dependency_.getToken(0, '.', i);
        if (i == -1)
            break;

        if (dependencyNS != dependentNS)
            break;
    }
    OUString entityName_ = entityName(dependencyName);
    bool sameDir = j == -1 && entityName_ != "Any" && entityName_ != "Sequence"
                   && entityName_ != "Type" && entityName_ != "XInterface";

    auto importedDependency
        = dependentNamespace_[dependent_ + "_"
                              + (sameDir ? entityName(dependencyName) : dependencyNS)]++;

    if (importedDependency != 0)
        return;

    if (sameDir) {
        out("import { " + entityName(dependencyName) + " } from './");
        out(entityName(dependencyName));
        out("';\n");
        return;
    }

    out("import *  as " + dependencyNS + " from '");

    // uno special case
    if (entityNamespace(dependencyName) == entityNamespace(dependentName)) {
        out("./';\n");
        return;
    }

    // for every differing component of the namespace, go up a directory
    while (j != -1) {
        dependent_.getToken(0, '.', j);
        out("../");
    }
    out(dependencyNS);
    out("';\n");
}

void V8WriterInternal::writeEnum(OUString const& name,
                                 rtl::Reference<unoidl::EnumTypeEntity> entity) {
    out("export declare enum " + entityName(name) + "{\n");
    for (auto& m : entity->getMembers()) {
        writeDoc(m.doc);
        out(m.name + ",\n");
    }
    out("};\n");
}

void V8WriterInternal::writePlainStruct(OUString const& name,
                                        rtl::Reference<unoidl::PlainStructTypeEntity> entity) {
    auto base = entity->getDirectBase();
    out("export interface " + entityName(name));
    if (!base.isEmpty()) {
        out(" extends ");
        writeName(base);
    }

    out("{\n");

    for (auto& m : entity->getDirectMembers()) {
        writeDoc(m.doc);
        out(m.name + ": ");
        writeType(m.type);
        out(",");
    }
    out("}\n");
}

void V8WriterInternal::writePolymorphicStruct(
    OUString const& name, rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity) {
    out("export type " + entityName(name) + "<");
    auto& typeParams = entity->getTypeParameters();
    for (auto i(typeParams.begin()); i != typeParams.end(); ++i) {
        if (i != typeParams.begin()) {
            out(",");
        }
        out(*i);
    }
    out("> = {\n");

    for (auto& j : entity->getMembers()) {
        writeDoc(j.doc);
        out(j.name + ": ");

        if (j.parameterized) {
            out(j.type);
        } else {
            writeType(j.type);
        }
        out(";\n");
    }
    out("}");
}

void V8WriterInternal::writeException(OUString const& name,
                                      rtl::Reference<unoidl::ExceptionTypeEntity> entity) {
    out("// exception " + name);
}

void V8WriterInternal::writeInterface(OUString const& name,
                                      rtl::Reference<unoidl::InterfaceTypeEntity> entity) {
    out("export interface " + entityName(name) + " extends BaseType {");

    for (auto& i : entity->getDirectAttributes()) {
        writeDoc(i.doc);
        if (i.readOnly)
            out("readonly ");

        out(" " + i.name + ": ");
        writeType(i.type);
        out(";\n");
    }

    for (auto& i : entity->getDirectMethods()) {
        writeDoc(i.doc);
        out(i.name + "(");
        bool hasOutParam = false;
        bool hasNonOutParam = false;
        for (auto k(i.parameters.begin()); k != i.parameters.end(); ++k) {
            if (k != i.parameters.begin()) {
                out(", ");
            }
            switch (k->direction) {
                case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_IN:
                    out(" " + k->name + ":");
                    writeType(k->type);
                    hasNonOutParam = true;
                    break;
                case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_OUT:
                case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_IN_OUT:
                    hasOutParam = true;
                    break;
            }
        }
        if (hasOutParam) {
            if (hasNonOutParam)
                out(", ");

            out("out: {");
            for (auto k(i.parameters.begin()); k != i.parameters.end(); ++k) {
                if (k != i.parameters.begin()) {
                    out(", ");
                }
                switch (k->direction) {
                    case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_IN:
                        break;
                    case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_OUT:
                    case unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_IN_OUT:
                        out(" " + k->name + ":");
                        writeType(k->type);
                        out(", ");
                        break;
                }
            }
            out("}");
        }
        out("): ");
        if (i.returnType == "void") {
            out("void");
        } else {
            writeType(i.returnType);
        }
        out(";\n");
    }
    out("}\n");

    if (!entity->getDirectOptionalBases().empty()) {
        out("type OptionalBase = {");
        for (auto& i : entity->getDirectOptionalBases()) {
            out("/** Attempts to cast this object as `" + simplifyNamespace(i.name)
                + "`, otherwise returns undefined */\n");
            out("as(type: '");
            out(simplifyNamespace(i.name));
            out("'): ");
            writeName(i.name);
            out(" | undefined,\n");
        }
        out("};\n");
    }

    auto& adjacent = entities_[name]->adjacentInterfaces;

    if (!adjacent.empty()) {
        out("type AdjacentBase = {\n");
        for (auto& i : adjacent) {
            out("/** Attempts to cast this object as `" + simplifyNamespace(i)
                + "`, otherwise returns undefined */\n");
            out("as(type: '");
            out(simplifyNamespace(i));
            out("'): ");
            writeName(i);
            out(" | undefined,\n");
        }
        out("};\n");
    }

    out("type BaseType = {}");

    for (auto& i : entity->getDirectMandatoryBases()) {
        out("\n & ");
        writeName(i.name);
    }

    if (!entity->getDirectOptionalBases().empty())
        out("\n & OptionalBase");

    if (!adjacent.empty())
        out("\n & AdjacentBase");

    out(";");
}

void V8WriterInternal::writeTypedef(OUString const& name,
                                    rtl::Reference<unoidl::TypedefEntity> entity) {
    out("export type " + entityName(name) + "=");
    writeType(entity->getType());
    out(";\n");
}

void V8WriterInternal::writeConstantGroup(OUString const& name,
                                          rtl::Reference<unoidl::ConstantGroupEntity> entity) {
    out("export declare const " + entityName(name) + ": Readonly<{\n");
    for (auto& i : entity->getMembers()) {
        writeDoc(i.doc);
        out(i.name + ": ");
        switch (i.value.type) {
            case unoidl::ConstantValue::TYPE_BOOLEAN:
                out("boolean");
                break;
            case unoidl::ConstantValue::TYPE_BYTE:
            case unoidl::ConstantValue::TYPE_SHORT:
            case unoidl::ConstantValue::TYPE_UNSIGNED_SHORT:
            case unoidl::ConstantValue::TYPE_LONG:
            case unoidl::ConstantValue::TYPE_UNSIGNED_LONG:
            case unoidl::ConstantValue::TYPE_FLOAT:
            case unoidl::ConstantValue::TYPE_DOUBLE:
                out("number");
                break;
            case unoidl::ConstantValue::TYPE_HYPER:
            case unoidl::ConstantValue::TYPE_UNSIGNED_HYPER:
                out("bigint");
                break;
        }
        out(",\n");
    }
    out("\n}>;");
}

void V8WriterInternal::writeSingleInterfaceService(
    OUString const& name, rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity) {
    out("export interface " + entityName(name) + " extends ");
    writeName(entity->getBase());
    out("{}");

    out("export declare const " + entityName(name) + ": {");
    for (auto& j : entity->getConstructors()) {
        writeDoc(j.doc);
        out(j.name + "(");
        for (auto k(j.parameters.begin()); k != j.parameters.end(); ++k) {
            if (k != j.parameters.begin()) {
                out(", ");
            }
            if (k->rest) {
                out("...");
            }
            out(k->name + ": ");
            writeType(k->type);
        }
        out("),\n");
    }
    out("};");
    out("// new-style service");
}

void V8WriterInternal::writeAccumulationService(
    OUString const& name, rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity) {
    std::vector<unoidl::AnnotatedReference> mandatoryBases{};
    std::vector<unoidl::AnnotatedReference> optionalBases{};
    std::vector<unoidl::InterfaceTypeEntity::Attribute> props{};
    std::vector<unoidl::AnnotatedReference> mbases;
    for (auto& i : entity->getDirectMandatoryBaseInterfaces()) {
        mbases.emplace_back(i.name, std::vector(i.annotations));
    }
    for (auto& i : entity->getDirectMandatoryBaseServices()) {
        mbases.emplace_back(i.name, std::vector(i.annotations));
    }
    std::vector<unoidl::AnnotatedReference> obases;
    for (auto& i : entity->getDirectOptionalBaseInterfaces()) {
        obases.emplace_back(i.name, std::vector(i.annotations));
    }
    for (auto& i : entity->getDirectOptionalBaseServices()) {
        obases.emplace_back(i.name, std::vector(i.annotations));
    }
    std::vector<unoidl::InterfaceTypeEntity::Attribute> directAttributes;
    for (auto& i : entity->getDirectProperties()) {
        directAttributes.emplace_back(i.name, i.type, i.attributes & i.ATTRIBUTE_BOUND,
                                      i.attributes & i.ATTRIBUTE_READ_ONLY, std::vector<OUString>(),
                                      std::vector<OUString>(), std::vector<OUString>());
    }
    std::vector<unoidl::InterfaceTypeEntity::Method> directMethods;
    std::vector<OUString> ann(entity->getAnnotations());

    writeInterface(name, new unoidl::InterfaceTypeEntity(
                             entity->isPublished(), std::move(mbases), std::move(obases),
                             std::move(directAttributes), std::move(directMethods), std::move(ann),
                             std::move(entity->getDoc())));
    out("// old-style service, no bindings generated - for reference only");
}

void V8WriterInternal::writeInterfaceSingleton(
    OUString const& name, rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity) {
    out("export interface " + entityName(name) + " extends ");
    writeName(entity->getBase());
    out("{}");
}

void V8WriterInternal::writeServiceSingleton(
    OUString const& name, rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity) {
    out("export interface " + entityName(name) + " extends ");
    writeName(entity->getBase());
    out("{}");
}

void V8WriterInternal::writeHeader() {
    createEntityFile("unov8", ".hxx");
    out(R"(
#ifndef UNOV8_INTERNAL_HXX_
#define UNOV8_INTERNAL_HXX_
#include <cppu/unotype.hxx>
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

} // namespace unov8
#endif
)");
    close();
}

}
