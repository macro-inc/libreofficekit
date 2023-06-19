#include "osl/file.hxx"
#include "rtl/ref.hxx"
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <unoidl/unoidl.hxx>
#include <map>
#include <set>
#include <unordered_set>

namespace writer {
struct Entity {
    enum class Sorted { NO, ACTIVE, YES };
    enum class Written { NO, DECLARATION, DEFINITION };

    explicit Entity(rtl::Reference<unoidl::Entity> const& theEntity, bool theRelevant)
        : entity(theEntity)
        , relevant(theRelevant)
        , sorted(Sorted::NO)
        , written(Written::NO) {}

    rtl::Reference<unoidl::Entity> const entity;
    std::set<OUString> dependencies;
    std::set<OUString> interfaceDependencies;
    std::map<OUString, Entity*> module;
    std::set<OUString> adjacentInterfaces;
    bool relevant;
    Sorted sorted;
    Written written;
};

/**
 * Takes a string <type>: produces the <rank> (eg. 0 = float, 1 = sequence<float>, etc.),
 * a vector of <typeArguments> for polymorphic structs, and whether the type is an IDL-defined
 * <entity> (interface, struct, service, basically any non-primitive)
 */
OUString decomposeType(OUString const& type, std::size_t* rank,
                       std::vector<OUString>* typeArguments, bool* entity);

void mapEntities(rtl::Reference<unoidl::Manager> const& manager, OUString const& uri,
                 std::map<OUString, writer::Entity*>& map,
                 std::map<OUString, writer::Entity*>& flatMap);

OUString entityName(OUString const& name);
OUString entityNamespace(OUString const& name);
OUString simplifyNamespace(OUString const& name);
OUString cName(OUString const& name);
OUString cStructName(OUString const& name);
bool shouldSkipMethod(const unoidl::InterfaceTypeEntity::Method& method);

class BaseWriter {
public:
    BaseWriter(std::map<OUString, Entity*> entities, OUString const& outDirectoryUrl)
        : entities_(entities)
        , outDirectoryUrl_(outDirectoryUrl){};
    virtual void writeEntity(OUString const& name);
    virtual ~BaseWriter() = default;
    void createEntityFile(OUString const& entityName, OUString const& suffix);
    void close();
    bool isEntityOfSort(OUString const& entityName, unoidl::Entity::Sort sort);

protected:
    std::map<OUString, Entity*> entities_;
    OUString outDirectoryUrl_;
    OUString currentEntity_;
    std::map<OUString, int> dependentNamespace_{};
    void out(OUString const& text);
    OUString resolveTypedef(OUString const& type);

    virtual void writeDoc(rtl::Reference<unoidl::Entity> const& entity);
    virtual void writeDoc(OUString const& doc);

    virtual void writeName(OUString const& name) = 0;
    virtual OUString translateSimpleType(OUString const& name) = 0;
    virtual void writeType(OUString const& name) = 0;
    /** Declares interface dependencies/imports/includes */
    virtual void writeInterfaceDependency(OUString const& dependentName,
                                          OUString const& dependencyName, bool published)
        = 0;
    virtual void writeEnum(OUString const& name, rtl::Reference<unoidl::EnumTypeEntity> entity) = 0;
    virtual void writePlainStruct(OUString const& name,
                                  rtl::Reference<unoidl::PlainStructTypeEntity> entity)
        = 0;
    virtual void
    writePolymorphicStruct(OUString const& name,
                           rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity)
        = 0;
    virtual void writeException(OUString const& name,
                                rtl::Reference<unoidl::ExceptionTypeEntity> entity)
        = 0;
    virtual void writeInterface(OUString const& name,
                                rtl::Reference<unoidl::InterfaceTypeEntity> entity)
        = 0;
    virtual void writeTypedef(OUString const& name, rtl::Reference<unoidl::TypedefEntity> entity)
        = 0;
    virtual void writeConstantGroup(OUString const& name,
                                    rtl::Reference<unoidl::ConstantGroupEntity> entity)
        = 0;
    virtual void
    writeSingleInterfaceService(OUString const& name,
                                rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity)
        = 0;
    virtual void
    writeAccumulationService(OUString const& name,
                             rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity)
        = 0;
    virtual void
    writeInterfaceSingleton(OUString const& name,
                            rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity)
        = 0;
    virtual void writeServiceSingleton(OUString const& name,
                                       rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity)
        = 0;

private:
    osl::File* file_;
};

class V8Writer : public BaseWriter {
public:
    V8Writer(std::map<OUString, Entity*> entities, OUString const& outDirectoryUrl,
             std::vector<OUString> sorted)
        : BaseWriter(entities, outDirectoryUrl)
        , sorted_(sorted) {}
    void writeBuildFile();
    // really used like sort entity to organize in specific namespaces
    void writeEntity(OUString const& name);
    void writeDeclarations();
    void writeSimpleTypeConverter();
    void writeAnyTypeConverter();
    void writeHeaderIncludes();
    void writeClassIncludes();
    // call after iterating with writeEntity on sorted
    void writeOrganizedEntities();

protected:
    void writeName(OUString const& name);
    OUString translateSimpleType(OUString const& name);
    void writeType(OUString const& name);
    void writeInterfaceDependency(OUString const& dependentName, OUString const& dependencyName,
                                  bool published);
    void writeEnum(OUString const& name, rtl::Reference<unoidl::EnumTypeEntity> entity);
    void writePlainStruct(OUString const& name,
                          rtl::Reference<unoidl::PlainStructTypeEntity> entity);
    void writePolymorphicStruct(OUString const& name,
                                rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity);
    void writeException(OUString const& name, rtl::Reference<unoidl::ExceptionTypeEntity> entity);
    void writeInterface(OUString const& name, rtl::Reference<unoidl::InterfaceTypeEntity> entity);
    void writeTypedef(OUString const& name, rtl::Reference<unoidl::TypedefEntity> entity);
    void writeConstantGroup(OUString const& name,
                            rtl::Reference<unoidl::ConstantGroupEntity> entity);
    void
    writeSingleInterfaceService(OUString const& name,
                                rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity);
    void writeAccumulationService(OUString const& name,
                                  rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity);
    void writeInterfaceSingleton(OUString const& name,
                                 rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity);
    void writeServiceSingleton(OUString const& name,
                               rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity);
    void writeMethodParamsConstructors(const unoidl::InterfaceTypeEntity::Method& method);
    void writeMethodParamsDestructors(const unoidl::InterfaceTypeEntity::Method& method);
    void writeInterfaceMethodBuilder(OUString const& name, unoidl::InterfaceTypeEntity* entity, std::unordered_set<int>& declared);
    void writeInterfaceMethods(OUString const& name, OUString const& className, unoidl::InterfaceTypeEntity* entity,
                               std::unordered_set<int>& declared);
    void writeMethodReturn(OUString const& type);
    void writeInterfaceDeclaration(OUString const& name, unoidl::InterfaceTypeEntity* entity);
    void writeInterfaceMethodsDeclaration(unoidl::InterfaceTypeEntity* entity,
                                          std::unordered_set<int>& declared);
    void writeStructDeclaration(OUString const& name);

    std::vector<OUString> sorted_;
    std::map<OUString, unoidl::InterfaceTypeEntity*> interfaces_;
    std::map<OUString, unoidl::EnumTypeEntity*> enums_;
    std::map<OUString, unoidl::ConstantGroupEntity*> constgroups_;
    std::map<OUString, unoidl::PlainStructTypeEntity*> structs_;
};

class TypeScriptWriter : public BaseWriter {
public:
    TypeScriptWriter(std::map<OUString, Entity*> entities, OUString const& outDirectoryUrl)
        : BaseWriter(entities, outDirectoryUrl) {}
    void writeTSIndex(OUString const& name, Entity* moduleEntity);

private:
    void writeName(OUString const& name);
    OUString translateSimpleType(OUString const& name);
    void writeType(OUString const& name);
    void writeInterfaceDependency(OUString const& dependentName, OUString const& dependencyName,
                                  bool published);
    void writeEnum(OUString const& name, rtl::Reference<unoidl::EnumTypeEntity> entity);
    void writePlainStruct(OUString const& name,
                          rtl::Reference<unoidl::PlainStructTypeEntity> entity);
    void writePolymorphicStruct(OUString const& name,
                                rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity);
    void writeException(OUString const& name, rtl::Reference<unoidl::ExceptionTypeEntity> entity);
    void writeInterface(OUString const& name, rtl::Reference<unoidl::InterfaceTypeEntity> entity);
    void writeTypedef(OUString const& name, rtl::Reference<unoidl::TypedefEntity> entity);
    void writeConstantGroup(OUString const& name,
                            rtl::Reference<unoidl::ConstantGroupEntity> entity);
    void
    writeSingleInterfaceService(OUString const& name,
                                rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity);
    void writeAccumulationService(OUString const& name,
                                  rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity);
    void writeInterfaceSingleton(OUString const& name,
                                 rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity);
    void writeServiceSingleton(OUString const& name,
                               rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity);
};

class V8WriterInternal : public V8Writer {
public:
    V8WriterInternal(std::map<OUString, Entity*> entities, OUString const& outDirectoryUrl,
                     std::vector<OUString> sorted)
        : V8Writer(entities, outDirectoryUrl, sorted) {}
    void writeInternalHeader();
    void writeSharedHeader();

private:
    void writeName(OUString const& name);
    void writeInterfaceStaticName(OUString const& name,
                               rtl::Reference<unoidl::InterfaceTypeEntity> entity, bool isRef,
                               std::unordered_set<int>& declared);
    void writeInterfaceMethods(OUString const& name,
                               rtl::Reference<unoidl::InterfaceTypeEntity> entity, bool isRef,
                               std::unordered_set<int>& declared);
    void writeInterfaceMethodsInit(OUString const& name,
                               rtl::Reference<unoidl::InterfaceTypeEntity> entity, bool isRef,
                               std::unordered_set<int>& declared);
    void writeCStructToCpp(OUString const& name);
    void writeCppStructToC(OUString const& name);
    void writeCToCpp(OUString const& type, OUString const& name);
    void writeCppToC(OUString const& type, OUString const& name);
    void writeMethodParams(const unoidl::InterfaceTypeEntity::Method& method);
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
