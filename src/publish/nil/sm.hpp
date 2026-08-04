#pragma once

#include <nil/xalt/checks.hpp>
#include <nil/xalt/coalesce.hpp>
#include <nil/xalt/str_name.hpp>
#include <nil/xalt/tlist.hpp>
#include <nil/xalt/typed.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <queue>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace nil::sm
{
    struct state_metadata final
    {
        std::size_t region = 0;
        std::size_t region_count = 0;
        std::string_view name;
        const state_metadata* parent = nullptr;
    };

    namespace detail
    {
        template <typename T>
        consteval auto str_short_name_no_template()
        {
            constexpr auto name = nil::xalt::str_short_name<T>();
            constexpr auto name_sv = nil::xalt::literal_sv<name>;
            constexpr auto lt = name_sv.find('<');
            if constexpr (lt == std::string_view::npos)
            {
                return name;
            }
            else
            {
                return nil::xalt::substr<name, 0, lt>();
            }
        }

        template <typename T>
        inline constexpr const auto& str_short_name_no_template_v
            = nil::xalt::literal_v<str_short_name_no_template<T>()>;

        template <typename T>
        inline constexpr const auto& str_short_name_no_template_sv
            = nil::xalt::literal_sv<str_short_name_no_template<T>()>;

        template <typename T>
        inline constexpr std::string_view state_name_sv = []() -> std::string_view
        {
            if constexpr (requires() { T::name; })
            {
                return T::name;
            }
            else
            {
                return str_short_name_no_template_sv<T>;
            }
        }();

        template <typename T>
        void deleter(void* v)
        {
            delete static_cast<T*>(v); // NOLINT
        }

        template <typename T>
        void* cloner(void* v)
        {
            return new T(*static_cast<T*>(v)); // NOLINT
        }

        class Queues;
        struct Contexts;
        struct IState;

        struct EvRegionsFinalized final
        {
            // This is the state instance owned by State<T>
            const void* target = nullptr;
        };

        struct Transit final
        {
            std::unique_ptr<IState> (*to)(
                void*,
                Queues*,
                Contexts*,
                std::size_t,
                const state_metadata* //
            );
        };

        struct Emit final
        {
            const void* id = nullptr;
            void (*deleter)(void*) = nullptr;
            void* (*cloner)(void*) = nullptr;
            void* data = nullptr;
        };
    }

    struct fin final
    {
        static constexpr auto name = "[**]";
    };

    template <typename... R>
    struct root final
    {
        static constexpr auto name = "[--]";
        using regions = nil::xalt::tlist<R...>;
    };

    struct Unhandled final
    {
    };

    struct Terminate final
    {
    };

    struct Forward final
    {
    };

    struct Defer final
    {
    };

    struct Discard final
    {
    };

    struct NOOP final
    {
    };

    template <typename T>
    struct Transit final
    {
        using type = T;
    };

    template <typename T>
    struct Emit final
    {
        static_assert(
            std::copy_constructible<T>,
            "Events emitted through Emit must be copy constructible."
        );

        template <typename... Args>
        explicit Emit(Args&&... args)
            : id(nil::xalt::type_id<T>)
            , deleter(&detail::deleter<T>)
            , cloner(&detail::cloner<T>)
            , data(new T{std::forward<Args>(args)...})
        {
        }

        Emit(Emit&& o) noexcept
            : id(o.id)
            , deleter(o.deleter)
            , cloner(o.cloner)
            , data(std::exchange(o.data, nullptr))
        {
        }

        Emit& operator=(Emit&& o) noexcept
        {
            if (this != &o)
            {
                id = o.id;
                deleter = o.deleter;
                cloner = o.cloner;
                data = std::exchange(o.data, nullptr);
            }
            return *this;
        }

        Emit(const Emit& o) = delete;
        Emit& operator=(const Emit& o) = delete;

        ~Emit()
        {
            if (data != nullptr)
            {
                deleter(data);
            }
        }

        detail::Emit cast()
        {
            return detail::Emit{id, deleter, cloner, std::exchange(data, nullptr)};
        }

    private:
        using type = T;
        const void* id = nullptr;
        void (*deleter)(void*) = nullptr;
        void* (*cloner)(void*) = nullptr;
        void* data = nullptr;
    };

    namespace concepts
    {
        template <typename T>
        concept is_allowed_to_use_for_on_event         //
            = std::is_same_v<T, Terminate>             //
            || std::is_same_v<T, Forward>              //
            || std::is_same_v<T, Defer>                //
            || std::is_same_v<T, Discard>              //
            || nil::xalt::is_of_template_v<T, Transit> //
            || nil::xalt::is_of_template_v<T, Emit>;

        template <typename T>
        struct is_allowed_to_use_for_react_as_predicate final
        {
            static constexpr bool value = is_allowed_to_use_for_on_event<T>;
        };

        template <typename T>
        concept is_allowed_to_use_for_on_event_result
            = is_allowed_to_use_for_on_event<std::remove_cvref_t<T>>
            || (nil::xalt::is_of_template_v<std::remove_cvref_t<T>, std::variant>
                && nil::xalt::to_tlist_t<std::remove_cvref_t<T>>::template all_of<
                    is_allowed_to_use_for_react_as_predicate>);

        template <typename T, typename E>
        concept has_on_event = requires(T t, E event) {
            { t.on_event(event) } -> is_allowed_to_use_for_on_event_result;
        };

        template <typename T>
        concept is_allowed_to_use_for_lifecycle_hook
            = std::is_same_v<T, NOOP> || nil::xalt::is_of_template_v<T, Emit>;

        template <typename T>
        struct is_allowed_to_use_for_lifecycle_hook_as_predicate final
        {
            static constexpr bool value = is_allowed_to_use_for_lifecycle_hook<T>;
        };

        template <typename T>
        concept has_on_enter = requires(T t) {
            { t.on_enter() } -> is_allowed_to_use_for_lifecycle_hook;
        } || requires(T t) {
            requires nil::xalt::is_of_template_v<decltype(t.on_enter()), std::variant>;
            requires nil::xalt::to_tlist_t<decltype(t.on_enter()
            )>::template all_of<is_allowed_to_use_for_lifecycle_hook_as_predicate>;
        };

        template <typename T>
        concept has_on_exit = requires(T t) {
            { t.on_exit() } -> is_allowed_to_use_for_lifecycle_hook;
        } || requires(T t) {
            requires nil::xalt::is_of_template_v<decltype(t.on_exit()), std::variant>;
            requires nil::xalt::to_tlist_t<decltype(t.on_exit()
            )>::template all_of<is_allowed_to_use_for_lifecycle_hook_as_predicate>;
        };

        template <typename T>
        concept is_allowed_to_use_for_on_regions_finalized //
            = std::is_same_v<T, NOOP>                      //
            || std::is_same_v<T, Terminate>                //
            || nil::xalt::is_of_template_v<T, Transit>     //
            || nil::xalt::is_of_template_v<T, Emit>;

        template <typename T>
        struct is_allowed_to_use_for_on_regions_finalized_as_predicate final
        {
            static constexpr bool value = is_allowed_to_use_for_on_regions_finalized<T>;
        };

        template <typename T>
        concept has_on_regions_finalized = requires(T t) {
            { t.on_regions_finalized() } -> is_allowed_to_use_for_on_regions_finalized;
        } || requires(T t) {
            requires nil::xalt::is_of_template_v<decltype(t.on_regions_finalized()), std::variant>;
            requires nil::xalt::to_tlist_t<decltype(t.on_regions_finalized()
            )>::template all_of<is_allowed_to_use_for_on_regions_finalized_as_predicate>;
        };
    }

    namespace detail
    {
        NIL_XALT_COALESCE_TAG(regions, nil::xalt::tlist<>);
        NIL_XALT_COALESCE_TAG(events, nil::xalt::tlist<>);

        using on_event_t = std::variant<
            Terminate,
            Forward,
            Discard,
            Unhandled,
            Defer,
            detail::Transit,
            detail::Emit //
            >;
        using on_enter_t = std::variant<NOOP, detail::Emit>;
        using on_exit_t = std::variant<NOOP, detail::Emit>;
        using on_regions_finalized_t = std::variant<NOOP, Terminate, detail::Transit, detail::Emit>;

        struct IState
        {
            explicit IState(state_metadata init_metadata)
                : metadata(init_metadata)
            {
            }

            IState(IState&&) = delete;
            IState(const IState&) = delete;
            IState& operator=(IState&&) = delete;
            IState& operator=(const IState&) = delete;
            virtual ~IState() = default;

            virtual on_event_t on_event(const detail::Emit& e) = 0;

            const state_metadata metadata;
        };

        template <typename S, typename E>
        struct event_dispatcher;

        template <typename S, typename... E>
        struct event_dispatcher<S, nil::xalt::tlist<E...>>
        {
        private:
            using api_t = typename S::api_t;
            using state_t = typename api_t::state_t;
            using api_context_t = typename api_t::api_context_t;

            struct event_handler
            {
                const void* id = nullptr;
                on_event_t (*invoke)(state_t&, const void*, void*) = nullptr;
            };

            template <typename EV>
            static on_event_t call(state_t& state_value, const void* event, void* api_contexts)
            {
                static_assert(
                    requires() {
                        {
                            api_t::template on_event<EV>(
                                state_value,
                                *static_cast<const EV*>(event),
                                static_cast<api_context_t*>(api_contexts)
                            )
                        } -> concepts::is_allowed_to_use_for_on_event_result;
                    },
                    "API must expose on_event<Event>(state_value, event, contexts...) with an "
                    "allowed "
                    "return type"
                );
                auto result = api_t::template on_event<EV>(
                    state_value,
                    *static_cast<const EV*>(event),
                    static_cast<api_context_t*>(api_contexts)
                );
                return S::template to_runtime_action_as<on_event_t>(result);
            }

            static constexpr auto handlers = std::array<event_handler, sizeof...(E)>{
                event_handler{.id = nil::xalt::type_id<E>, .invoke = &call<E>}...
            };

        public:
            static on_event_t dispatch(
                const detail::Emit& event,
                state_t& state,
                api_context_t* api_contexts
            )
            {
                for (const auto& handler : handlers)
                {
                    if (handler.id == event.id)
                    {
                        return handler.invoke(state, event.data, api_contexts);
                    }
                }

                return Unhandled{};
            }
        };

        class Queues
        {
        public:
            Queues() = default;
            Queues(Queues&&) = delete;
            Queues(const Queues&) = delete;
            Queues& operator=(Queues&&) = delete;
            Queues& operator=(const Queues&) = delete;

            void push_emit(detail::Emit e)
            {
                emit.push(e);
            }

            void push_defer(detail::Emit e)
            {
                defer.push(e);
            }

            void flush(IState& state)
            {
                while (!defer.empty())
                {
                    auto emitted = defer.front();
                    defer.pop();
                    state.on_event(emitted);
                    emitted.deleter(emitted.data);
                }

                while (!emit.empty())
                {
                    auto emitted = emit.front();
                    emit.pop();
                    state.on_event(emitted);
                    emitted.deleter(emitted.data);
                }
            }

            ~Queues()
            {
                while (!defer.empty())
                {
                    auto emitted = defer.front();
                    defer.pop();
                    emitted.deleter(emitted.data);
                }

                while (!emit.empty())
                {
                    auto emitted = emit.front();
                    emit.pop();
                    emitted.deleter(emitted.data);
                }
            }

        private:
            std::queue<detail::Emit> emit;
            std::queue<detail::Emit> defer;
        };

        struct Region
        {
            std::unique_ptr<IState> active_state;
            std::vector<detail::Emit> deferred;
            bool terminated = false;
        };

        struct Contexts
        {
            void* state;
            void* api;
        };
    }

    template <typename T, template <typename...> typename API>
    class State: public detail::IState
    {
    public:
        using api_t = API<T>;
        using self_t = State<T, API>;
        using metadata_t = state_metadata;

    private:
        using state_t = typename api_t::state_t;
        using regions_t = typename api_t::regions_t;
        using events_t = typename api_t::events_t;
        using event_dispatch_t = detail::event_dispatcher<self_t, events_t>;
        using state_context_t = typename api_t::state_context_t;
        using api_context_t = typename api_t::api_context_t;
        using region_index_t = std::size_t;

        struct sub_state_scan_t
        {
            bool handle = false;
            bool forward = false;
            std::array<detail::on_event_t, regions_t::size> results = {};
        };

        template <typename U>
        static std::unique_ptr<IState> make_state(
            void* p,
            detail::Queues* queues,
            detail::Contexts* contexts,
            std::size_t region,
            const state_metadata* parent_metadata
        )
        {
            auto parent = static_cast<state_t*>(p);
            return std::make_unique<State<U, API>>(
                parent,
                queues,
                contexts,
                region,
                parent_metadata
            );
        }

        template <typename... R, std::size_t... I>
        static std::array<detail::Region, regions_t::size> init_regions_impl(
            self_t* self,
            detail::Queues* qs,
            detail::Contexts* contexts,
            nil::xalt::tlist<R...> /* regions */,
            std::index_sequence<I...> /* region indices */
        )
        {
            return std::array<detail::Region, regions_t::size>{detail::Region{
                make_state<R>(
                    std::addressof(self->current_state),
                    qs,
                    contexts,
                    I,
                    std::addressof(self->metadata)
                ),
                {},
                false
            }...};
        }

        template <typename... R>
        static std::array<detail::Region, regions_t::size> init_regions(
            [[maybe_unused]] self_t* self,
            [[maybe_unused]] detail::Queues* qs,
            [[maybe_unused]] detail::Contexts* contexts,
            nil::xalt::tlist<R...> /* regions */
        )
        {
            auto on_enter_result = self->on_enter();
            if (std::holds_alternative<detail::Emit>(on_enter_result))
            {
                qs->push_emit(std::get<detail::Emit>(on_enter_result));
            }

            if constexpr (sizeof...(R) > 0)
            {
                return init_regions_impl(
                    self,
                    qs,
                    contexts,
                    nil::xalt::tlist<R...>{},
                    std::index_sequence_for<R...>{}
                );
            }
            else
            {
                return {};
            }
        }

    public:
        template <typename Parent>
        explicit State(
            Parent* init_parent,
            detail::Queues* init_qs,
            detail::Contexts* init_contexts,
            std::size_t init_region,
            const metadata_t* init_parent_metadata
        )
            : detail::IState(state_metadata{
                  .region = init_region,
                  .region_count = regions_t::size,
                  .name = detail::state_name_sv<T>,
                  .parent = init_parent_metadata
              })
            , qs(init_qs)
            , contexts(init_contexts)
            , current_state(api_t::make(
                  init_parent,
                  static_cast<state_context_t*>(contexts->state),
                  static_cast<api_context_t*>(contexts->api),
                  this->metadata
              ))
            , regions(init_regions(this, qs, contexts, regions_t()))
        {
        }

        State(State&&) = delete;
        State(const State&) = delete;
        State& operator=(State&&) = delete;
        State& operator=(const State&) = delete;

        ~State() override
        {
            for (auto it = regions.rbegin(); it != regions.rend(); ++it)
            {
                it->active_state.reset();
            }

            for (auto i = 0U; i < regions_t::size; ++i)
            {
                const auto index = i;
                flush_deferred(index);
            }

            auto on_exit_result = on_exit();
            if (std::holds_alternative<detail::Emit>(on_exit_result))
            {
                qs->push_emit(std::get<detail::Emit>(on_exit_result));
            }
        }

        detail::on_event_t on_event(const detail::Emit& e) override
        {
            const auto is_regions_finalized_event
                = e.id == nil::xalt::type_id<detail::EvRegionsFinalized>;
            if (is_regions_finalized_event)
            {
                const auto* ev = static_cast<const detail::EvRegionsFinalized*>(e.data);
                if (ev->target == std::addressof(current_state))
                {
                    finalized = true;
                    return std::visit(
                        []<typename V>(V v) -> detail::on_event_t
                        {
                            if constexpr (std::is_same_v<NOOP, V>)
                            {
                                return Discard();
                            }
                            else
                            {
                                return v;
                            }
                        },
                        on_regions_finalized()
                    );
                }
            }

            auto sub_state = scan_regions(e);

            if (!is_regions_finalized_event && sub_state.handle)
            {
                auto this_result = event_dispatch_t::dispatch(
                    e,
                    current_state,
                    static_cast<api_context_t*>(contexts->api)
                );
                if (std::holds_alternative<Unhandled>(this_result))
                {
                    apply_sub_transits(e, sub_state.results);
                    if (sub_state.forward)
                    {
                        return Forward{};
                    }

                    return Unhandled{};
                }

                if (!std::holds_alternative<detail::Transit>(this_result))
                {
                    apply_sub_transits(e, sub_state.results);
                }

                if (std::holds_alternative<detail::Emit>(this_result))
                {
                    qs->push_emit(std::get<detail::Emit>(this_result));
                    return Discard{};
                }

                return this_result;
            }

            apply_sub_transits(e, sub_state.results);
            return Discard{};
        }

    private:
        detail::Queues* qs;
        detail::Contexts* contexts;
        state_t current_state;
        std::array<detail::Region, regions_t::size> regions;
        bool finalized = false;

        detail::on_enter_t on_enter()
        {
            auto result
                = api_t::on_enter(current_state, static_cast<api_context_t*>(contexts->api));
            return to_runtime_action_as<detail::on_enter_t>(result);
        }

        detail::on_exit_t on_exit()
        {
            auto result = api_t::on_exit(current_state, static_cast<api_context_t*>(contexts->api));
            return to_runtime_action_as<detail::on_exit_t>(result);
        }

        detail::on_regions_finalized_t on_regions_finalized()
        {
            auto result = api_t::on_regions_finalized(
                current_state,
                static_cast<api_context_t*>(contexts->api)
            );
            return to_runtime_action_as<detail::on_regions_finalized_t>(result);
        }

        sub_state_scan_t scan_regions(const detail::Emit& e)
        {
            sub_state_scan_t scan = {};
            auto all_unhandled = true;
            for (auto i = 0U; i < regions_t::size; ++i)
            {
                scan.results[i] = regions[i].active_state->on_event(e);
                std::visit(
                    [&]<typename R>(const R& /* r */)
                    {
                        if constexpr (std::is_same_v<R, Forward>)
                        {
                            scan.forward = true;
                            all_unhandled = false;
                        }
                        else if constexpr (!std::is_same_v<R, Unhandled>)
                        {
                            all_unhandled = false;
                        }
                    },
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                    scan.results[i]
                );
            }

            scan.handle = scan.forward || all_unhandled;

            return scan;
        }

    public:
        template <typename O, typename R>
        static O to_runtime_action_as(R& r)
        {
            if constexpr (nil::xalt::is_of_template_v<std::remove_cvref_t<R>, std::variant>)
            {
                return std::visit([]<typename V>(V& v) { return to_runtime_action_as<O>(v); }, r);
            }
            else if constexpr (nil::xalt::is_of_template_v<R, ::nil::sm::Transit>)
            {
                return O{detail::Transit{.to = &make_state<typename R::type>}};
            }
            else if constexpr (nil::xalt::is_of_template_v<R, ::nil::sm::Emit>)
            {
                return O{r.cast()};
            }
            else
            {
                return O{r};
            }
        }

    private:
        void flush_deferred(std::size_t region_idx)
        {
            auto& deferred = regions[region_idx].deferred;
            for (auto& event : deferred)
            {
                qs->push_defer(event);
            }
            deferred.clear();
        }

        void apply_sub_transits(
            const detail::Emit& e,
            std::array<detail::on_event_t, regions_t::size>& sub_state_result
        )
        {
            for (auto i = 0U; i < regions_t::size; ++i)
            {
                std::visit(
                    [&]<typename R>(R& r)
                    {
                        if constexpr (std::is_same_v<R, detail::Transit>)
                        {
                            const auto parent = std::addressof(current_state);
                            regions[i].active_state.reset();
                            regions[i].active_state
                                = r.to(parent, qs, contexts, i, std::addressof(this->metadata));
                            // Flush deferred events after state transition
                            flush_deferred(i);
                        }
                        else if constexpr (std::is_same_v<R, detail::Emit>)
                        {
                            qs->push_emit(r);
                        }
                        else if constexpr (std::is_same_v<R, Terminate>)
                        {
                            const auto parent = std::addressof(current_state);
                            regions[i].active_state.reset();
                            regions[i].active_state = make_state<fin>(
                                parent,
                                qs,
                                contexts,
                                i,
                                std::addressof(this->metadata)
                            );
                            regions[i].terminated = true;
                            // Flush deferred events after state termination
                            flush_deferred(i);
                        }
                        else if constexpr (std::is_same_v<R, Defer>)
                        {
                            regions[i].deferred.push_back(detail::Emit{
                                .id = e.id,
                                .deleter = e.deleter,
                                .cloner = e.cloner,
                                .data = e.cloner(e.data),
                            });
                        }
                    },
                    sub_state_result[i]
                );
            }

            if (!finalized && !regions.empty()
                && std::all_of(
                    regions.begin(),
                    regions.end(),
                    [](const auto& r) { return r.terminated; }
                ))
            {
                qs->push_emit( //
                    Emit<detail::EvRegionsFinalized>(std::addressof(current_state)).cast()
                );
            }
        }
    };

    template <typename T, typename S = void, typename A = void>
    struct default_api final
    {
        using state_t = T;
        using state_context_t = S;
        using api_context_t = A;
        using regions_t = nil::xalt::coalesce_t<T, detail::regions_tag>;
        using events_t = nil::xalt::coalesce_t<T, detail::events_tag>;

        template <typename Parent>
        static state_t make(
            Parent* parent,
            state_context_t* state_contexts,
            api_context_t* /* api_contexts */,
            state_metadata /* metadata */
        )
        {
            if constexpr (requires() { T(parent, state_contexts); })
            {
                return T(parent, state_contexts);
            }
            else if constexpr (requires() { T(parent); })
            {
                return T(parent);
            }
            else
            {
                static_assert(
                    std::is_default_constructible_v<T>,
                    "State must be constructible from parent/contexts or "
                    "default-constructible"
                );
                return T{};
            }
        }

        template <typename E>
        static auto on_event(
            state_t& state,
            const E& event,
            api_context_t* /* api_contexts */
        )
        {
            if constexpr (concepts::has_on_event<state_t, E>)
            {
                return state.on_event(event);
            }
            else
            {
                return Unhandled();
            }
        }

        static auto on_enter(state_t& state, api_context_t* /* api_contexts */)
        {
            if constexpr (concepts::has_on_enter<state_t>)
            {
                return state.on_enter();
            }
            else
            {
                return NOOP();
            }
        }

        static auto on_exit(state_t& state, api_context_t* /* api_contexts */)
        {
            if constexpr (concepts::has_on_exit<state_t>)
            {
                return state.on_exit();
            }
            else
            {
                return NOOP();
            }
        }

        static auto on_regions_finalized(state_t& state, api_context_t* /* api_contexts */)
        {
            if constexpr (concepts::has_on_regions_finalized<state_t>)
            {
                return state.on_regions_finalized();
            }
            else
            {
                return NOOP();
            }
        }
    };

    template <template <typename...> typename API>
    struct coalesce_api final
    {
        template <typename T>
        struct type final
        {
            using inner_t = T;
            NIL_XALT_COALESCE_TAG(state_context_t, void*)
            NIL_XALT_COALESCE_TAG(api_context_t, void*)

            using state_context_t = nil::xalt::coalesce_t<API<T>, state_context_t_tag>;
            using api_context_t = nil::xalt::coalesce_t<API<T>, api_context_t_tag>;

            using defaulter_t = default_api<inner_t, state_context_t, api_context_t>;
            NIL_XALT_COALESCE_TAG(state_t, defaulter_t::state_t)
            NIL_XALT_COALESCE_TAG(events_t, defaulter_t::events_t)
            NIL_XALT_COALESCE_TAG(regions_t, defaulter_t::regions_t)

            using state_t = nil::xalt::coalesce_t<T, state_t_tag>;
            using regions_t = nil::xalt::coalesce_t<T, regions_t_tag>;
            using events_t = nil::xalt::coalesce_t<T, events_t_tag>;

            template <typename Parent>
            static state_t make(
                Parent* parent,
                state_context_t* state_contexts,
                api_context_t* api_contexts,
                const state_metadata& metadata
            )
            {
                static constexpr auto api_has_make
                    = requires() { API<T>::make(parent, state_contexts, api_contexts, metadata); };

                if constexpr (api_has_make)
                {
                    return API<T>::make(parent, state_contexts, api_contexts, metadata);
                }
                else
                {
                    return defaulter_t::make(parent, state_contexts, api_contexts, metadata);
                }
            }

            template <typename E>
            static auto on_event(state_t& state, const E& event, api_context_t* api_contexts)
            {
                if constexpr (requires() { API<T>::on_event(state, event, api_contexts); })
                {
                    return API<T>::on_event(state, event, api_contexts);
                }
                else
                {
                    return defaulter_t::on_event(state, event, api_contexts);
                }
            }

            static auto on_enter(state_t& state, api_context_t* api_contexts)
            {
                if constexpr (requires() { API<T>::on_enter(state, api_contexts); })
                {
                    return API<T>::on_enter(state, api_contexts);
                }
                else
                {
                    return defaulter_t::on_enter(state, api_contexts);
                }
            }

            static auto on_exit(state_t& state, api_context_t* api_contexts)
            {
                if constexpr (requires() { API<T>::on_exit(state, api_contexts); })
                {
                    return API<T>::on_exit(state, api_contexts);
                }
                else
                {
                    return defaulter_t::on_exit(state, api_contexts);
                }
            }

            static auto on_regions_finalized(state_t& state, api_context_t* api_contexts)
            {
                if constexpr (requires() { API<T>::on_regions_finalized(state, api_contexts); })
                {
                    return API<T>::on_regions_finalized(state, api_contexts);
                }
                else
                {
                    return defaulter_t::on_regions_finalized(state, api_contexts);
                }
            }
        };
    };

    template <template <typename...> typename API, typename... Regions>
    class SM final
    {
        using api_t = API<root<Regions...>>;
        using regions_t = typename api_t::regions_t;
        using state_context_t = typename api_t::state_context_t;
        using api_context_t = typename api_t::api_context_t;

    public:
        explicit SM(state_context_t* state_contexts, api_context_t* api_contexts)
            : contexts({.state = state_contexts, .api = api_contexts})
            , state(this, &queues, &contexts, 0U, nullptr)
        {
        }

        ~SM() noexcept = default;

        SM(const SM&) = delete;
        SM& operator=(const SM&) = delete;

        SM(SM&&) noexcept = default;
        SM& operator=(SM&&) noexcept = default;

        template <typename T>
            requires(!std::is_same_v<std::remove_cvref_t<T>, detail::Emit>)
        void post(T event)
        {
            state.on_event(detail::Emit{
                .id = nil::xalt::type_id<T>,
                .deleter = &detail::deleter<T>,
                .cloner = &detail::cloner<T>,
                .data = &event
            });

            queues.flush(state);
        }

    private:
        detail::Queues queues;
        detail::Contexts contexts;
        State<root<Regions...>, API> state;
    };

    template <typename... Regions>
    using DefaultSM = SM<default_api, Regions...>;

    template <template <typename...> typename API, typename... Regions>
    using CoalescedSM = SM<coalesce_api<API>::template type, Regions...>;
}
