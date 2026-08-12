#include "BehaviourValidationInternal.h"

namespace ReplayEngine::Runtime::Validation::Detail::BehaviourValidation
{
        void RegisterProbe()
        {
            Core::RegisterBuiltInComponents();
            Core::ComponentRegistry::Register<LifecycleProbeBehaviour>(
                Core::ComponentTypeInfo::Describe("Lifecycle Probe", "Internal")
                    .HiddenInEditor()
                    .AllowMultipleInstances()
                    .WithTypeGUID(LifecycleProbeBehaviour::StaticTypeGUID())
                    .InModule("RePlayEngine.Validation"));
            BehaviourRegistry::Register(LifecycleProbeBehaviour::StaticTypeGUID(),
                BehaviourRegistry::Native());
        }

        bool ContainsInOrder(const std::vector<std::string>& calls,
            const char* first, const char* second)
        {
            std::size_t first_index = calls.size();
            for (std::size_t i = 0; i < calls.size(); ++i)
            {
                if (calls[i] == first) { first_index = i; break; }
            }
            if (first_index == calls.size()) return false;
            for (std::size_t i = first_index + 1; i < calls.size(); ++i)
            {
                if (calls[i] == second) return true;
            }
            return false;
        }
}
