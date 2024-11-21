#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_TOFINO_CONTROL_PLANE_ARCHITECTURE_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_TOFINO_CONTROL_PLANE_ARCHITECTURE_H_
#include "control-plane/p4RuntimeArchStandard.h"

namespace P4::ControlPlaneAPI::Helpers {

using namespace P4::literals;

/// @ref CounterlikeTraits<> specialization for @ref CounterExtern for TNA
template <>
struct CounterlikeTraits<Standard::CounterExtern<Standard::Arch::TNA>> {
    static const cstring name() { return "counter"_cs; }
    static const cstring directPropertyName() { return "tna_direct_counter"_cs; }
    static const cstring typeName() { return "Counter"_cs; }
    static const cstring directTypeName() { return "DirectCounter"_cs; }
    static const cstring sizeParamName() { return "size"_cs; }
    static p4configv1::CounterSpec::Unit mapUnitName(const cstring name) {
        using p4configv1::CounterSpec;
        if (name == "PACKETS")
            return CounterSpec::PACKETS;
        else if (name == "BYTES")
            return CounterSpec::BYTES;
        else if (name == "PACKETS_AND_BYTES")
            return CounterSpec::BOTH;
        return CounterSpec::UNSPECIFIED;
    }
    // the index of the type parameter for the counter index, in the type
    // parameter list of the extern type declaration.
    static std::optional<size_t> indexTypeParamIdx() { return 1; }
};

/// @ref CounterlikeTraits<> specialization for @ref MeterExtern for TNA
template <>
struct CounterlikeTraits<Standard::MeterExtern<Standard::Arch::TNA>> {
    static const cstring name() { return "meter"_cs; }
    static const cstring directPropertyName() { return "tna_direct_meter"_cs; }
    static const cstring typeName() { return "Meter"_cs; }
    static const cstring directTypeName() { return "DirectMeter"_cs; }
    static const cstring sizeParamName() { return "size"_cs; }
    static p4configv1::MeterSpec::Unit mapUnitName(const cstring name) {
        using p4configv1::MeterSpec;
        if (name == "PACKETS")
            return MeterSpec::PACKETS;
        else if (name == "BYTES")
            return MeterSpec::BYTES;
        return MeterSpec::UNSPECIFIED;
    }
    // the index of the type parameter for the meter index, in the type
    // parameter list of the extern type declaration.
    static std::optional<size_t> indexTypeParamIdx() { return 0; }
};
}  // namespace P4::ControlPlaneAPI::Helpers

namespace P4::ControlPlaneAPI::Standard {
template <>
struct ActionProfileTraits<Arch::TNA> {
    static const cstring name() { return "action profile"_cs; }
    static const cstring propertyName() { return "tna_implementation"_cs; }
    static const cstring typeName() { return "ActionProfile"_cs; }
    static const cstring sizeParamName() { return "size"_cs; }
};

template <>
struct ActionSelectorTraits<Arch::TNA> : public ActionProfileTraits<Arch::TNA> {
    static const cstring name() { return "action selector"_cs; }
    static const cstring typeName() { return "ActionSelector"_cs; }
    static const cstring actionProfileName() { return "action_profile"_cs; }
};

template <>
struct RegisterTraits<Arch::TNA> {
    static const cstring name() { return "register"_cs; }
    static const cstring typeName() { return "Register"_cs; }
    static const cstring sizeParamName() { return "size"_cs; }
    static size_t dataTypeParamIdx() { return 0; }
    // the index of the type parameter for the register index, in the type
    // parameter list of the extern type declaration.
    static std::optional<size_t> indexTypeParamIdx() { return 1; }
};
}  // namespace P4::ControlPlaneAPI::Standard

namespace P4::ControlPlaneAPI::Standard {
/// Implements @ref P4RuntimeArchHandlerIface for the Tofino architecture. The
/// overridden methods will be called by the @P4RuntimeSerializer to collect and
/// serialize tofino-specific symbols which are exposed to the control-plane.
class P4RuntimeArchHandlerTofino final : public P4RuntimeArchHandlerIface {
 public:
    P4RuntimeArchHandlerTofino(ReferenceMap *refMap, TypeMap *typeMap,
                               const IR::ToplevelBlock *evaluatedProgram);

 protected:
    using ArchCounterExtern = CounterExtern<Arch::TNA>;
    using CounterTraits = Helpers::CounterlikeTraits<ArchCounterExtern>;
    using ArchMeterExtern = MeterExtern<Arch::TNA>;
    using MeterTraits = Helpers::CounterlikeTraits<ArchMeterExtern>;

    using Counter = p4configv1::Counter;
    using Meter = p4configv1::Meter;
    using CounterSpec = p4configv1::CounterSpec;
    using MeterSpec = p4configv1::MeterSpec;

    void collectTableProperties(P4RuntimeSymbolTableIface *symbols,
                                const IR::TableBlock *tableBlock) override {
        CHECK_NULL(tableBlock);
        auto table = tableBlock->container;
        bool isConstructedInPlace = false;

        {
            auto instance =
                getExternInstanceFromProperty(table, ActionProfileTraits<Arch::TNA>::propertyName(),
                                              refMap, typeMap, &isConstructedInPlace);
            if (instance) {
                if (instance->type->name != ActionProfileTraits<Arch::TNA>::typeName() &&
                    instance->type->name != ActionSelectorTraits<Arch::TNA>::typeName()) {
                    error(ErrorType::ERR_EXPECTED,
                          "Expected an action profile or action selector: %1%",
                          instance->expression);
                } else if (isConstructedInPlace) {
                    symbols->add(SymbolType::P4RT_ACTION_PROFILE(), *instance->name);
                }
            }
        }
        {
            auto instance = getExternInstanceFromProperty(
                table, CounterTraits::directPropertyName(), refMap, typeMap, &isConstructedInPlace);
            if (instance) {
                if (instance->type->name != CounterTraits::directTypeName()) {
                    error(ErrorType::ERR_EXPECTED, "Expected a direct counter: %1%",
                          instance->expression);
                } else if (isConstructedInPlace) {
                    symbols->add(SymbolType::P4RT_DIRECT_COUNTER(), *instance->name);
                }
            }
        }
        {
            auto instance = getExternInstanceFromProperty(table, MeterTraits::directPropertyName(),
                                                          refMap, typeMap, &isConstructedInPlace);
            if (instance) {
                if (instance->type->name != MeterTraits::directTypeName()) {
                    error(ErrorType::ERR_EXPECTED, "Expected a direct meter: %1%",
                          instance->expression);
                } else if (isConstructedInPlace) {
                    symbols->add(SymbolType::P4RT_DIRECT_METER(), *instance->name);
                }
            }
        }
    }

    void collectAssignmentStatement(P4RuntimeSymbolTableIface *,
                                    const IR::AssignmentStatement *) override {}

    void collectExternMethod(P4RuntimeSymbolTableIface *, const P4::ExternMethod *) override {}

    void collectExternInstance(P4RuntimeSymbolTableIface *symbols,
                               const IR::ExternBlock *externBlock) override {
        CHECK_NULL(externBlock);

        auto decl = externBlock->node->to<IR::IDeclaration>();
        // Skip externs instantiated inside table declarations (as properties);
        // that should only apply to action profiles / selectors since direct
        // resources cannot be constructed in place for PSA.
        if (decl == nullptr) return;

        if (externBlock->type->name == CounterTraits::typeName()) {
            symbols->add(SymbolType::P4RT_COUNTER(), decl);
        } else if (externBlock->type->name == CounterTraits::directTypeName()) {
            symbols->add(SymbolType::P4RT_DIRECT_COUNTER(), decl);
        } else if (externBlock->type->name == MeterTraits::typeName()) {
            symbols->add(SymbolType::P4RT_METER(), decl);
        } else if (externBlock->type->name == MeterTraits::directTypeName()) {
            symbols->add(SymbolType::P4RT_DIRECT_METER(), decl);
        } else if (externBlock->type->name == ActionProfileTraits<Arch::TNA>::typeName() ||
                   externBlock->type->name == ActionSelectorTraits<Arch::TNA>::typeName()) {
            symbols->add(SymbolType::P4RT_ACTION_PROFILE(), decl);
        } else if (externBlock->type->name == RegisterTraits<Arch::TNA>::typeName()) {
            symbols->add(SymbolType::P4RT_REGISTER(), decl);
        }
    }

    void collectExternFunction(P4RuntimeSymbolTableIface *symbols,
                               const P4::ExternFunction *externFunction) override {
        // no common task
        (void)symbols;
        (void)externFunction;
    }

    void collectExtra(P4RuntimeSymbolTableIface *symbols) override {
        // nothing to do for standard architectures
        (void)symbols;
    }

    void postCollect(const P4RuntimeSymbolTableIface &symbols) override {
        (void)symbols;
        // analyze action profiles and build a mapping from action profile name
        // to the set of tables referencing them
        Helpers::forAllEvaluatedBlocks(evaluatedProgram, [&](const IR::Block *block) {
            if (!block->is<IR::TableBlock>()) return;
            auto table = block->to<IR::TableBlock>()->container;
            auto implementation = getTableImplementationName(table, refMap);
            if (implementation)
                actionProfilesRefs[*implementation].insert(table->controlPlaneName());
        });
    }

    void addTableProperties(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4info,
                            p4configv1::Table *table, const IR::TableBlock *tableBlock) override {
        CHECK_NULL(tableBlock);
        auto tableDeclaration = tableBlock->container;

        using Helpers::isExternPropertyConstructedInPlace;

        auto implementation = getActionProfile(tableDeclaration, refMap, typeMap);
        auto directCounter =
            Helpers::getDirectCounterlike<ArchCounterExtern>(tableDeclaration, refMap, typeMap);
        auto directMeter =
            Helpers::getDirectCounterlike<ArchMeterExtern>(tableDeclaration, refMap, typeMap);

        if (implementation) {
            auto id = symbols.getId(SymbolType::P4RT_ACTION_PROFILE(), implementation->name);
            table->set_implementation_id(id);
            auto propertyName = ActionProfileTraits<Arch::TNA>::propertyName();
            if (isExternPropertyConstructedInPlace(tableDeclaration, propertyName))
                addActionProfile(symbols, p4info, *implementation);
        }

        if (directCounter) {
            auto id = symbols.getId(SymbolType::P4RT_DIRECT_COUNTER(), directCounter->name);
            table->add_direct_resource_ids(id);
            // no risk to add twice because direct counters cannot be shared
            addCounter(symbols, p4info, *directCounter);
        }

        if (directMeter) {
            auto id = symbols.getId(SymbolType::P4RT_DIRECT_METER(), directMeter->name);
            table->add_direct_resource_ids(id);
            // no risk to add twice because direct meters cannot be shared
            addMeter(symbols, p4info, *directMeter);
        }
    }

    void addExternInstance(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4info,
                           const IR::ExternBlock *externBlock) override {
        auto decl = externBlock->node->to<IR::Declaration_Instance>();
        if (decl == nullptr) return;

        auto p4RtTypeInfo = p4info->mutable_type_info();
        if (externBlock->type->name == CounterTraits::typeName()) {
            auto counter = Helpers::Counterlike<ArchCounterExtern>::from(externBlock, refMap,
                                                                         typeMap, p4RtTypeInfo);
            if (counter) addCounter(symbols, p4info, *counter);
        } else if (externBlock->type->name == MeterTraits::typeName()) {
            auto meter = Helpers::Counterlike<ArchMeterExtern>::from(externBlock, refMap, typeMap,
                                                                     p4RtTypeInfo);
            if (meter) addMeter(symbols, p4info, *meter);
        } else if (externBlock->type->name == RegisterTraits<Arch::TNA>::typeName()) {
            auto register_ = Register::from<Arch::TNA>(externBlock, refMap, typeMap, p4RtTypeInfo);
            if (register_) addRegister(symbols, p4info, *register_);
        } else if (externBlock->type->name == ActionProfileTraits<Arch::TNA>::typeName() ||
                   externBlock->type->name == ActionSelectorTraits<Arch::TNA>::typeName()) {
            auto actionProfile = getActionProfile(externBlock);
            if (actionProfile) addActionProfile(symbols, p4info, *actionProfile);
        }
    }

    void addExternFunction(const P4RuntimeSymbolTableIface &, p4configv1::P4Info *,
                           const P4::ExternFunction *) override {}

    void postAdd(const P4RuntimeSymbolTableIface &, ::p4::config::v1::P4Info *) override {}

    void addExternEntries(const p4::v1::WriteRequest *, const P4RuntimeSymbolTableIface &,
                          const IR::ExternBlock *) override {}
    bool filterAnnotations(cstring) override { return false; }

    google::protobuf::util::JsonPrintOptions getJsonPrintOptions() override {
        return jsonPrintOptions;
    }

    static std::optional<ActionProfile> getActionProfile(cstring name, const IR::Type_Extern *type,
                                                         int64_t size,
                                                         const IR::IAnnotated *annotations) {
        ActionProfileType actionProfileType;
        if (type->name == ActionSelectorTraits<Arch::TNA>::typeName()) {
            actionProfileType = ActionProfileType::INDIRECT_WITH_SELECTOR;
        } else if (type->name == ActionProfileTraits<Arch::TNA>::typeName()) {
            actionProfileType = ActionProfileType::INDIRECT;
        } else {
            return std::nullopt;
        }

        return ActionProfile{name, actionProfileType, size, annotations};
    }

    /// @return the action profile referenced in @table's implementation property,
    /// if it has one, or std::nullopt otherwise.
    static std::optional<ActionProfile> getActionProfile(const IR::P4Table *table,
                                                         ReferenceMap *refMap, TypeMap *typeMap) {
        auto propertyName = ActionProfileTraits<Arch::TNA>::propertyName();
        auto instance = getExternInstanceFromProperty(table, propertyName, refMap, typeMap);
        if (!instance) return std::nullopt;
        auto size =
            instance->substitution.lookupByName(ActionProfileTraits<Arch::TNA>::sizeParamName())
                ->expression;
        if (!size->template is<IR::Constant>()) {
            error(ErrorType::ERR_INVALID, "Action profile '%1%' has non-constant size '%2%'",
                  *instance->name, size);
            return std::nullopt;
        }
        return getActionProfile(*instance->name, instance->type,
                                size->template to<IR::Constant>()->asInt(),
                                getTableImplementationAnnotations(table, refMap));
    }

    /// @return the action profile declared with @decl
    static std::optional<ActionProfile> getActionProfile(const IR::ExternBlock *instance) {
        auto decl = instance->node->to<IR::IDeclaration>();
        const IR::CompileTimeValue *size = nullptr;
        // Deprecated ActionSelector uses `size` as first arg, while new version uses
        // `action_profile` to specify size.
        if (instance->type->name == ActionSelectorTraits<Arch::TNA>::typeName()) {
            auto firstArgName = instance->getConstructorParameters()->parameters.at(0)->name.name;
            if (firstArgName == "size") {
                size =
                    instance->getParameterValue(ActionSelectorTraits<Arch::TNA>::sizeParamName());
            } else {
                auto actionProfile = instance->getParameterValue(
                    ActionSelectorTraits<Arch::TNA>::actionProfileName());
                size = actionProfile->template to<IR::ExternBlock>()->getParameterValue(
                    ActionProfileTraits<Arch::TNA>::sizeParamName());
            }
        } else if (instance->type->name == ActionProfileTraits<Arch::TNA>::typeName()) {
            size = instance->getParameterValue(ActionProfileTraits<Arch::TNA>::sizeParamName());
        }
        if (!size->template is<IR::Constant>()) {
            error(ErrorType::ERR_INVALID, "Action profile '%1%' has non-constant size '%2%'",
                  decl->controlPlaneName(), size);
            return std::nullopt;
        }
        return getActionProfile(decl->controlPlaneName(), instance->type,
                                size->template to<IR::Constant>()->asInt(),
                                decl->to<IR::IAnnotated>());
    }

    void addActionProfile(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4Info,
                          const ActionProfile &actionProfile) {
        auto profile = p4Info->add_action_profiles();
        auto id = symbols.getId(SymbolType::P4RT_ACTION_PROFILE(), actionProfile.name);
        setPreamble(profile->mutable_preamble(), id, actionProfile.name,
                    symbols.getAlias(actionProfile.name), actionProfile.annotations,
                    // exclude @max_group_size if present
                    [](cstring name) { return name == "max_group_size"; });
        profile->set_with_selector(actionProfile.type == ActionProfileType::INDIRECT_WITH_SELECTOR);
        profile->set_size(actionProfile.size);
        const auto *maxGroupSizeAnnotation =
            actionProfile.annotations->getAnnotation("max_group_size"_cs);
        if (maxGroupSizeAnnotation != nullptr) {
            if (actionProfile.type == ActionProfileType::INDIRECT_WITH_SELECTOR) {
                const auto *maxGroupSizeConstant =
                    maxGroupSizeAnnotation->getExpr(0)->to<IR::Constant>();
                CHECK_NULL(maxGroupSizeConstant);
                profile->set_max_group_size(maxGroupSizeConstant->asInt());
            } else {
                warning(ErrorType::WARN_IGNORE,
                        "Ignoring annotation @max_group_size on action profile '%1%', "
                        "which does not have a selector",
                        actionProfile.annotations);
            }
        }

        auto tablesIt = actionProfilesRefs.find(actionProfile.name);
        if (tablesIt != actionProfilesRefs.end()) {
            for (const auto &table : tablesIt->second)
                profile->add_table_ids(symbols.getId(P4RuntimeSymbolType::P4RT_TABLE(), table));
        }
    }

    /// Set common fields between Counter and DirectCounter.
    template <typename Kind>
    void setCounterCommon(const P4RuntimeSymbolTableIface &symbols, Kind *counter, p4rt_id_t id,
                          const Helpers::Counterlike<ArchCounterExtern> &counterInstance) {
        setPreamble(counter->mutable_preamble(), id, counterInstance.name,
                    symbols.getAlias(counterInstance.name), counterInstance.annotations);
        auto counter_spec = counter->mutable_spec();
        counter_spec->set_unit(CounterTraits::mapUnitName(counterInstance.unit));
    }

    void addCounter(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4Info,
                    const Helpers::Counterlike<ArchCounterExtern> &counterInstance) {
        if (counterInstance.table) {
            auto counter = p4Info->add_direct_counters();
            auto id = symbols.getId(SymbolType::P4RT_DIRECT_COUNTER(), counterInstance.name);
            setCounterCommon(symbols, counter, id, counterInstance);
            auto tableId = symbols.getId(P4RuntimeSymbolType::P4RT_TABLE(), *counterInstance.table);
            counter->set_direct_table_id(tableId);
        } else {
            auto counter = p4Info->add_counters();
            auto id = symbols.getId(SymbolType::P4RT_COUNTER(), counterInstance.name);
            setCounterCommon(symbols, counter, id, counterInstance);
            counter->set_size(counterInstance.size);
            if (counterInstance.index_type_name) {
                counter->mutable_index_type_name()->set_name(counterInstance.index_type_name);
            }
        }
    }

    /// Set common fields between Meter and DirectMeter.
    template <typename Kind>
    void setMeterCommon(const P4RuntimeSymbolTableIface &symbols, Kind *meter, p4rt_id_t id,
                        const Helpers::Counterlike<ArchMeterExtern> &meterInstance) {
        setPreamble(meter->mutable_preamble(), id, meterInstance.name,
                    symbols.getAlias(meterInstance.name), meterInstance.annotations);
        auto meter_spec = meter->mutable_spec();
        meter_spec->set_unit(MeterTraits::mapUnitName(meterInstance.unit));
    }

    void addMeter(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4Info,
                  const Helpers::Counterlike<ArchMeterExtern> &meterInstance) {
        if (meterInstance.table) {
            auto meter = p4Info->add_direct_meters();
            auto id = symbols.getId(SymbolType::P4RT_DIRECT_METER(), meterInstance.name);
            setMeterCommon(symbols, meter, id, meterInstance);
            auto tableId = symbols.getId(P4RuntimeSymbolType::P4RT_TABLE(), *meterInstance.table);
            meter->set_direct_table_id(tableId);
        } else {
            auto meter = p4Info->add_meters();
            auto id = symbols.getId(SymbolType::P4RT_METER(), meterInstance.name);
            setMeterCommon(symbols, meter, id, meterInstance);
            meter->set_size(meterInstance.size);
            if (meterInstance.index_type_name) {
                meter->mutable_index_type_name()->set_name(meterInstance.index_type_name);
            }
        }
    }

    void addRegister(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4Info,
                     const Register &registerInstance) {
        auto register_ = p4Info->add_registers();
        auto id = symbols.getId(SymbolType::P4RT_REGISTER(), registerInstance.name);
        setPreamble(register_->mutable_preamble(), id, registerInstance.name,
                    symbols.getAlias(registerInstance.name), registerInstance.annotations);
        register_->set_size(registerInstance.size);
        register_->mutable_type_spec()->CopyFrom(*registerInstance.typeSpec);
        if (registerInstance.index_type_name) {
            register_->mutable_index_type_name()->set_name(registerInstance.index_type_name);
        }
    }

    void addDigest(const P4RuntimeSymbolTableIface &symbols, p4configv1::P4Info *p4Info,
                   const Digest &digest) {
        // Each call to digest() creates a new digest entry in the P4Info.
        // Right now we only take the type of data included in the digest
        // (encoded in its name) into account, but it may be that we should also
        // consider the receiver.
        auto id = symbols.getId(SymbolType::P4RT_DIGEST(), digest.name);
        if (serializedInstances.find(id) != serializedInstances.end()) return;
        serializedInstances.insert(id);

        auto *digestInstance = p4Info->add_digests();
        setPreamble(digestInstance->mutable_preamble(), id, digest.name,
                    symbols.getAlias(digest.name), digest.annotations);
        digestInstance->mutable_type_spec()->CopyFrom(*digest.typeSpec);
    }

    /// @return the table implementation property, or nullptr if the table has no
    /// such property.
    static const IR::Property *getTableImplementationProperty(const IR::P4Table *table) {
        return table->properties->getProperty(ActionProfileTraits<Arch::TNA>::propertyName());
    }

    static const IR::IAnnotated *getTableImplementationAnnotations(const IR::P4Table *table,
                                                                   ReferenceMap *refMap) {
        // Cannot use auto here, otherwise the compiler seems to think that the
        // type of impl is dependent on the template parameter and we run into
        // this issue: https://stackoverflow.com/a/15572442/4538702
        const IR::Property *impl = getTableImplementationProperty(table);
        if (impl == nullptr) return nullptr;
        if (!impl->value->is<IR::ExpressionValue>()) return nullptr;
        auto expr = impl->value->to<IR::ExpressionValue>()->expression;
        if (expr->is<IR::ConstructorCallExpression>()) return impl->to<IR::IAnnotated>();
        if (expr->is<IR::PathExpression>()) {
            auto decl = refMap->getDeclaration(expr->to<IR::PathExpression>()->path, true);
            return decl->to<IR::IAnnotated>();
        }
        return nullptr;
    }

    static std::optional<cstring> getTableImplementationName(const IR::P4Table *table,
                                                             ReferenceMap *refMap) {
        const IR::Property *impl = getTableImplementationProperty(table);
        if (impl == nullptr) return std::nullopt;
        if (!impl->value->is<IR::ExpressionValue>()) {
            error(ErrorType::ERR_EXPECTED,
                  "Expected implementation property value for table %1% to be an expression: %2%",
                  table->controlPlaneName(), impl);
            return std::nullopt;
        }
        auto expr = impl->value->to<IR::ExpressionValue>()->expression;
        if (expr->is<IR::ConstructorCallExpression>()) return impl->controlPlaneName();
        if (expr->is<IR::PathExpression>()) {
            auto decl = refMap->getDeclaration(expr->to<IR::PathExpression>()->path, true);
            return decl->controlPlaneName();
        }
        return std::nullopt;
    }

    ReferenceMap *refMap;
    TypeMap *typeMap;
    const IR::ToplevelBlock *evaluatedProgram;

    std::unordered_map<cstring, std::set<cstring>> actionProfilesRefs;

    /// The extern instances we've serialized so far. Used for deduplication.
    std::set<p4rt_id_t> serializedInstances;

    // JSON printing options for serialization
    google::protobuf::util::JsonPrintOptions jsonPrintOptions;
};

/// The architecture handler builder implementation for Tofino.
struct TofinoArchHandlerBuilder : public P4RuntimeArchHandlerBuilderIface {
    P4RuntimeArchHandlerIface *operator()(ReferenceMap *refMap, TypeMap *typeMap,
                                          const IR::ToplevelBlock *evaluatedProgram) const override;
};

}  // namespace P4::ControlPlaneAPI::Standard

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_TOFINO_CONTROL_PLANE_ARCHITECTURE_H_ */
