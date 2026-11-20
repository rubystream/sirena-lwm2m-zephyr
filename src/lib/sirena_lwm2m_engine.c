#include <sirena/lwm2m/engine.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>

struct sirena_lwm2m_engine_request {
    enum sirena_lwm2m_engine_operation operation;
    void *payload;
    int result;
    struct k_sem completion;
    bool busy;
};

K_MSGQ_DEFINE(engine_queue, sizeof(struct sirena_lwm2m_engine_request *),
	      CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE, 4);
K_THREAD_STACK_DEFINE(engine_stack, CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE);

static struct sirena_lwm2m_engine_request requests[CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE];
static struct k_mutex engine_lock;
static struct k_thread engine_thread;
static sirena_lwm2m_engine_handler_t engine_handler;
static sirena_lwm2m_engine_poll_fn engine_poll_fn;
static k_tid_t engine_tid;
static bool engine_running;

static void engine_complete(struct sirena_lwm2m_engine_request *request)
{
    request->result = engine_handler(request->operation, request->payload);
    k_sem_give(&request->completion);
}

static void engine_entry(void *first, void *second, void *third)
{
    struct sirena_lwm2m_engine_request *request;
    sirena_lwm2m_engine_poll_fn poll_fn;

    (void)first;
    (void)second;
    (void)third;

    k_mutex_lock(&engine_lock, K_FOREVER);
    engine_tid = k_current_get();
    k_mutex_unlock(&engine_lock);

    while (true) {
        if (k_msgq_get(&engine_queue, &request, K_NO_WAIT) == 0) {
            engine_complete(request);
            continue;
        }

        k_mutex_lock(&engine_lock, K_FOREVER);
        poll_fn = engine_poll_fn;
        k_mutex_unlock(&engine_lock);

        if (poll_fn != NULL) {
            (void)poll_fn(CONFIG_SIRENA_LWM2M_ENGINE_POLL_INTERVAL_MS);
            continue;
        }

        k_msgq_get(&engine_queue, &request, K_FOREVER);
        engine_complete(request);
    }
}

int sirena_lwm2m_engine_start(sirena_lwm2m_engine_handler_t handler)
{
    if (handler == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&engine_lock, K_FOREVER);
    if (engine_running) {
        k_mutex_unlock(&engine_lock);
        return -EALREADY;
    }

    engine_handler = handler;
    engine_running = true;
    k_mutex_unlock(&engine_lock);

    k_thread_create(&engine_thread, engine_stack, CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE,
                    engine_entry, NULL, NULL, NULL, CONFIG_SIRENA_LWM2M_ENGINE_PRIORITY, 0,
                    K_NO_WAIT);

    return 0;
}

k_tid_t sirena_lwm2m_engine_thread_id(void)
{
    k_tid_t tid = NULL;

    k_mutex_lock(&engine_lock, K_FOREVER);
    if (engine_running) {
        tid = engine_tid;
    }
    k_mutex_unlock(&engine_lock);

    return tid;
}

int sirena_lwm2m_engine_set_poll_fn(sirena_lwm2m_engine_poll_fn poll_fn)
{
    k_mutex_lock(&engine_lock, K_FOREVER);
    engine_poll_fn = poll_fn;
    k_mutex_unlock(&engine_lock);

    return 0;
}

int sirena_lwm2m_engine_submit(enum sirena_lwm2m_engine_operation operation, void *payload)
{
    struct sirena_lwm2m_engine_request *request = NULL;
    size_t i;
    int result;

    k_mutex_lock(&engine_lock, K_FOREVER);
    if (!engine_running) {
        k_mutex_unlock(&engine_lock);
        return -ENOTSUP;
    }

    for (i = 0U; i < ARRAY_SIZE(requests); ++i) {
        if (!requests[i].busy) {
            request = &requests[i];
            request->busy = true;
            break;
        }
    }

    if (request == NULL) {
        k_mutex_unlock(&engine_lock);
        return -EAGAIN;
    }

    request->operation = operation;
    request->payload = payload;
    request->result = -EINPROGRESS;
    k_sem_init(&request->completion, 0, 1);
    k_msgq_put(&engine_queue, &request, K_NO_WAIT);
    k_mutex_unlock(&engine_lock);

    result = k_sem_take(&request->completion, K_FOREVER);
    if (result != 0) {
        result = -EINPROGRESS;
    } else {
        result = request->result;
    }

    k_mutex_lock(&engine_lock, K_FOREVER);
    request->busy = false;
    k_mutex_unlock(&engine_lock);

    return result;
}
