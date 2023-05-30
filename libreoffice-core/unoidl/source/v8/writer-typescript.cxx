#include "rtl/ustrbuf.hxx"
#include "rtl/ustring.hxx"
#include "writer.hxx"
#include <iostream>
#include <unoidl/unoidl.hxx>

namespace writer {
void TypeScriptWriter::writeName(OUString const& name) {
    if (entityNamespace(name) == entityNamespace(currentEntity_)
        && entityNamespace(name) != "com.sun.star.uno") {
        out(entityName(name));
    } else {
        out(simplifyNamespace(name));
    }
}

OUString TypeScriptWriter::translateSimpleType(OUString const& name) {
    if (name == "void")
        return "undefined";
    if (name == "boolean")
        return "boolean";
    if (name == "byte")
        return "number";
    if (name == "short")
        return "number";
    if (name == "unsigned short")
        return "number";
    if (name == "long")
        return "number";
    if (name == "unsigned long")
        return "number";
    if (name == "hyper")
        return "bigint";
    if (name == "unsigned hyper")
        return "bigint";
    if (name == "float")
        return "number";
    if (name == "double")
        return "number";
    if (name == "char")
        return "string";
    if (name == "string")
        return "string";
    if (name == "type")
        return "uno.Type";
    if (name == "any")
        return "uno.Any";

    return name;
}

void TypeScriptWriter::writeType(OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool entity;
    OUString nucl(decomposeType(name, &rank, &args, &entity));

    // Handle special cases of sequences
    if (rank > 0) {
        if (nucl == "byte") {
            rank--;
            nucl = "ArrayBuffer";
        } else if (nucl == "com.sun.star.beans.PropertyValue") {
            rank--;
            nucl = "Record<string, uno.Any>";
        }
    }

    for (std::size_t i = 0; i != rank; ++i) {
        out("uno.Sequence<");
    }
    if (entity) {
        writeName(nucl);
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

void TypeScriptWriter::writeInterfaceDependency(OUString const& dependentName,
                                                OUString const& dependencyName, bool published) {
    (void)published; // unused

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

void TypeScriptWriter::writeEnum(OUString const& name,
                                 rtl::Reference<unoidl::EnumTypeEntity> entity) {
    out("export declare enum " + entityName(name) + "{\n");
    for (auto& m : entity->getMembers()) {
        writeDoc(m.doc);
        out(m.name + ",\n");
    }
    out("};\n");
}

void TypeScriptWriter::writePlainStruct(OUString const& name,
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

void TypeScriptWriter::writePolymorphicStruct(
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

void TypeScriptWriter::writeException(OUString const& name,
                                      rtl::Reference<unoidl::ExceptionTypeEntity> /* entity */) {
    out("// exception " + name);
}

void TypeScriptWriter::writeInterface(OUString const& name,
                                      rtl::Reference<unoidl::InterfaceTypeEntity> entity) {
    bool isXPropertySet = name == "com.sun.star.beans.XPropertySet";

    if (isXPropertySet) {
        out("type XPropertySet<T extends uno.Service = uno.Service> = BaseType & {\n");
    } else {
        out("export interface " + entityName(name) + " extends BaseType {\n");
    }
    bool hasProperties = entity->getDirectAttributes().size() > 0;

    if (hasProperties) {
        out("properties: {\n");
        for (auto& i : entity->getDirectAttributes()) {
            writeDoc(i.doc);
            if (i.readOnly)
                out("readonly ");

            out(" " + i.name + ": ");
            writeType(i.type);
            out(";\n");
        }
        out("} & (BaseType extends uno.Service ? BaseType['properties'] : {}),\n");
    }

    for (auto& i : entity->getDirectMethods()) {
        if (shouldSkipMethod(i))
            continue;

        writeDoc(i.doc);
        // Override XPropertySet type params
        if (isXPropertySet) {
            if (i.name == "setPropertyValue") {
              out("setPropertyValue<K extends keyof T['properties'] = keyof T['properties'], V extends T['properties'][K] = T['properties'][K]>(k: K, v: V): void;\n");

              continue;
            } else if (i.name == "getPropertyValue") {
              out("getPropertyValue<K extends keyof T['properties'] = keyof T['properties'], V extends T['properties'][K] = T['properties'][K]>(k: K): V;\n");
              continue;
            }
        }

        out(i.name);
        if (i.returnType == "any") {
            out("<T extends uno.Any = uno.Any>");
        }
        out("(");

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
        } else if (i.returnType == "any") {
            out("T");
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

void TypeScriptWriter::writeTypedef(OUString const& name,
                                    rtl::Reference<unoidl::TypedefEntity> entity) {
    out("export type " + entityName(name) + "=");
    writeType(entity->getType());
    out(";\n");
}

void TypeScriptWriter::writeConstantGroup(OUString const& name,
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

void TypeScriptWriter::writeSingleInterfaceService(
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

void TypeScriptWriter::writeAccumulationService(
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
                                      std::vector<OUString>(), std::vector<OUString>(), i.doc);
    }
    std::vector<unoidl::InterfaceTypeEntity::Method> directMethods;
    std::vector<OUString> ann(entity->getAnnotations());

    writeInterface(name, new unoidl::InterfaceTypeEntity(
                             entity->isPublished(), std::move(mbases), std::move(obases),
                             std::move(directAttributes), std::move(directMethods), std::move(ann),
                             std::move(entity->getDoc())));
    out("// old-style service, no bindings generated - for reference only");
}

void TypeScriptWriter::writeInterfaceSingleton(
    OUString const& name, rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity) {
    out("export interface " + entityName(name) + " extends ");
    writeName(entity->getBase());
    out("{}");
}

void TypeScriptWriter::writeServiceSingleton(
    OUString const& name, rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity) {
    out("export interface " + entityName(name) + " extends ");
    writeName(entity->getBase());
    out("{}");
}

void TypeScriptWriter::writeTSIndex(OUString const& name, Entity* moduleEntity) {
    if (!moduleEntity->relevant) {
        std::cerr << "SKIPPING " << name << std::endl;
        return;
    }
    bool stillRelevant = false;
    rtl::OUStringBuffer indexBuffer;
    for (auto& i : moduleEntity->module) {
        if (i.second->relevant) {
            indexBuffer.append("export * as " + i.first + " from './" + i.first + "';\n");
            stillRelevant = true;
        }
    }
    for (auto& i : moduleEntity->dependencies) {
        // override uno.XInterface
        if (name == "com.sun.star.uno" && i == "XInterface")
            continue;

        auto x = entities_.find(name + "." + i);
        if (x != entities_.end() && x->second->relevant
            && x->second->entity->getSort() != unoidl::Entity::SORT_EXCEPTION_TYPE) {
            indexBuffer.append("export * from './"
                               + simplifyNamespace(i) + "';\n");

            stillRelevant = true;
        }
    }

    if (!stillRelevant) {
        moduleEntity->relevant = false;
        return;
    }
    createEntityFile(name + ".index", ".d.ts");
    out(indexBuffer.makeStringAndClear());

    // write the namespace export at the end of the top-level index.d.ts to expose the types
    if (name == "com.sun.star") {
        out("\n"
            "export as namespace LibreOffice;\n");
    }

    // write the bootstrap in uno/index.d.ts
    if (name == "com.sun.star.uno") {
        out(R"(
export interface Type {
  typeClass: import("./TypeClass").TypeClass;
  typeName: string;
}

export interface Service {
    properties: Record<string, any>
}

/** base interface of all UNO interfaces */
export interface XInterface {
  /**
   * Attempts to cast this object as `interface_`
   *
   * @param interface_ The constructor function for the interface type
   * @returns This object cast as `interface_` if the object supports it, otherwise undefined
   */
  as<T extends XInterface>(interface_: string): T | undefined;
}
export declare const XInterface: unique symbol;

/** A primitive type supported by UNO, including Type and XInterface which are foundations to all types and interfaces */
export type Primitive =
  | undefined
  | boolean
  | number
  | bigint
  | string
  | Type
  | XInterface
  | Record<string, any>;

type ToSequence<T extends Primitive> = T extends NonNullable<Primitive>
  ? T extends boolean
    ? boolean[]
    : T[]
  : never;

type SimpleSequence<T extends Primitive = Primitive> = ToSequence<T>;
type ToArray<Type> = Type extends any ? Type[] : never;
// Realistically only handles up to 3 dimensions
type NestedSequence<T extends Primitive = Primitive> =
  | ToArray<SimpleSequence<T>>
  | ToArray<ToArray<SimpleSequence>>;

/**
 * An array of a single type of UNO primitive, struct, or interface
 */
export type Sequence<
  T extends Primitive | SimpleSequence | NestedSequence =
    | Primitive
    | SimpleSequence
    | NestedSequence
> = T extends Primitive
  ? SimpleSequence<T>
  : T extends SimpleSequence<Primitive> | NestedSequence<Primitive>
  ? ToArray<T>
  : never;

/**
 * Any UNO primitive or sequence
 */
export type Any<T extends Sequence | Primitive = any> = T extends Sequence
  ? Sequence
  : T extends Primitive
  ? Primitive
  : never;
        )");
    }

    close();
}
}
