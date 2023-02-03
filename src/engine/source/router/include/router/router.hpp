#ifndef _ROUTER_ROUTER_HPP
#define _ROUTER_ROUTER_HPP

#include <atomic>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <blockingconcurrentqueue.h>

#include <baseTypes.hpp>
#include <store/istore.hpp>

#include "environmentManager.hpp"
#include "route.hpp"

namespace router
{
constexpr auto ROUTES_TABLE_NAME = "internal/router_table/0"; ///< Name of the routes table in the store
constexpr auto JSON_PATH_NAME = "/name";                      ///< Json path for the name of the route
constexpr auto JSON_PATH_PRIORITY = "/priority";              ///< Json path for the priority of the route
constexpr auto JSON_PATH_TARGET = "/target";                  ///< Json path for the target of the route

constexpr auto JSON_PATH_EVENT = "/event"; ///< Json path for the event for enqueue


/**
 * @brief Router class to manage routes and events
 *
 * The router is the main class of the router module. It manages the routes and has the logic to route the events to the
 * correct environment. Also it has the thread pool to process the events, and the environment manager to manage the
 * runtime environments (creation, destruction, and interaction).
 * Get the events from the queue, select the correct environment (with the route priority and conditions), and send the
 * event to the environment.
 */
class Router
{

private:
    using concurrentQueue = moodycamel::BlockingConcurrentQueue<base::Event>;

    /* Status */
    /**
     * @brief Map of routes, each route is a vector of expressions, each expression for each thread
     */
    std::unordered_map<std::string, std::size_t> m_namePriority;
    std::map<std::size_t, std::vector<Route>> m_priorityRoute;
    std::shared_mutex m_mutexRoutes;    ///< Mutex to protect the routes map
    std::atomic_bool m_isRunning;       ///< Flag to know if the router is running
    std::vector<std::thread> m_threads; ///< Vector of threads for the router

    /* Resources */
    std::shared_ptr<EnvironmentManager> m_environmentManager; ///< Environment manager
    std::shared_ptr<builder::Builder> m_builder;              ///< Builder
    std::shared_ptr<concurrentQueue> m_queue;                 ///< Queue to get events
    std::shared_ptr<store::IStore> m_store;                   ///< Store to get/save routes table

    /* Config */
    std::size_t m_numThreads; ///< Number of threads for the router

    /**
     * @brief Get a Json with the routes table
     *
     * Array of objects with the name, priority and target of each route
     * The array is sorted by priority
     * @return json::Json with the routes table
     */
    json::Json tableToJson();

    /**
     * @brief Dump the routes table to the store
     * @warning This method is not thread safe. This method exits the program if the store fails
     */
    void dumpTableToStorage();

    /* Api callbacks */
    /**
     * @brief API callback for route creation
     *
     * @param params Parameters for route creation ("/name"), optional priority ("/priority") to override the default
     * @return api::WazuhResponse with the result of the operation
     */
    api::WazuhResponse apiSetRoute(const json::Json& params);

    /**
     * @brief API callback for list routes
     * @param params none
     * @return api::WazuhResponse with the result of the operation, a list of  entries with the name, priority and
     * target
     *
     */
    api::WazuhResponse apiGetRoutes(const json::Json& params);

    /**
     * @brief API callback for route deletion
     *
     * @param params Parameters for route deletion ("/name")
     * @return api::WazuhResponse with the result of the operation
     */
    api::WazuhResponse apiDeleteRoute(const json::Json& params);

    /**
     * @brief API callback for route priority change
     *
     * @param params Parameters for route priority change ("/name"), new priority ("/priority")
     * @return api::WazuhResponse with the result of the operation
     */
    api::WazuhResponse apiChangeRoutePriority(const json::Json& params);

    /**
     * @brief API callback for push an event to the router
     *
     * @param params Parameters for event push ("/event")
     * @return api::WazuhResponse with the result of the operation
     */
    api::WazuhResponse apiEnqueueEvent(const json::Json& params);

public:
    Router(std::shared_ptr<builder::Builder> builder, std::shared_ptr<store::IStore> store, std::size_t threads = 1)
        : m_mutexRoutes {}
        , m_namePriority {}
        , m_priorityRoute {}
        , m_isRunning {false}
        , m_numThreads {threads}
        , m_store {store}
        , m_threads {}
        , m_builder {builder}
    {
        if (threads == 0)
        {
            throw std::runtime_error("Router: The number of threads must be greater than 0.");
        }

        if (builder == nullptr)
        {
            throw std::runtime_error("Router: Builder can't be null.");
        }

        m_environmentManager = std::make_shared<EnvironmentManager>(builder, threads);

        auto result = m_store->get(ROUTES_TABLE_NAME);
        if (std::holds_alternative<base::Error>(result))
        {
            const auto error = std::get<base::Error>(result);
            WAZUH_LOG_DEBUG("Router: Routes table not found in store. Creating new table. {}", error.message);
            m_store->add(ROUTES_TABLE_NAME, json::Json {"[]"});
            return;
        }
        else
        {
            const auto table = std::get<json::Json>(result).getArray();
            if (!table.has_value())
            {
                throw std::runtime_error("Can't get routes table from store. Invalid table format.");
            }

            for (const auto& jRoute : *table)
            {
                const auto name = jRoute.getString(JSON_PATH_NAME);
                const auto priority = jRoute.getInt(JSON_PATH_PRIORITY);
                const auto target = jRoute.getString(JSON_PATH_TARGET);
                if (!name.has_value() || !priority.has_value() || !target.has_value())
                {
                    throw std::runtime_error("Router: Can't get routes table from store. Invalid table format");
                }

                const auto err = addRoute(name.value(), target.value(), priority.value());
                if (err.has_value())
                {
                    WAZUH_LOG_WARN("Router: couldn't add route " + name.value() + " to the router: {}",
                                   err.value().message);
                }
            }
        }
        // check if the table is empty
        if (getRouteTable().empty())
        {
            // Add default route
            WAZUH_LOG_WARN("There is no environment loaded. Events will be written in disk once the queue is full.");
        }
    };

    /**
     * @brief Get the list of route names, priority and target
     *
     * @return std::unordered_set<std::string>
     */
    std::vector<std::tuple<std::string, std::size_t, std::string>> getRouteTable();

    /**
     * @brief Change the priority of a route
     *
     * @param name name of the route
     * @param priority new priority
     * @return std::optional<base::Error> A error with description if the route can't be changed
     */
    std::optional<base::Error> changeRoutePriority(const std::string& name, int priority);

    /**
     * @brief Add a new route to the router.
     *
     * Optionally, the priority can be specified. If not, the priority is the especified in the route.
     * If the route already exists or the priority is already used, the route is not
     * added.
     * @param name name of the route
     * @return A error with description if the route can't be added
     * // TODO: FIX ALL DOC
     */
    std::optional<base::Error> addRoute(const std::string& routeName, const std::string& envName, int priority);

    /**
     * @brief Push an event to the queue of the router
     *
     * @param event event to push to the queue
     * @return std::optional<base::Error> A error with description if the event can't be pushed
     */
    std::optional<base::Error> enqueueEvent(base::Event event);

    /**
     * @brief Delete a route from the router
     *
     * @param name name of the route
     * @return A error with description if the route can't be deleted
     */
    std::optional<base::Error> removeRoute(const std::string& name);

    /**
     * @brief Launch in a new threads the router to ingest data from the queue.
     */
    std::optional<base::Error> run(std::shared_ptr<concurrentQueue> queue);

    /**
     * @brief Stop the router
     *
     * Send a stop signal to the router and wait for the threads to finish.
     */
    void stop();

    /**
     * @brief Main API callback for environment management
     *
     * @return api::CommandFn
     */
    api::CommandFn apiCallbacks();
};
} // namespace router
#endif // _ROUTER_ROUTER_HPP