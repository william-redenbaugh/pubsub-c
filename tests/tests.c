#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "pubsub.h"

static void check_leak(void) {
	ps_clean_sticky();
	assert(ps_stats_live_msg() == 0);
	assert(ps_stats_live_subscribers() == 0);
}

static void *inc_thread(void *v) {
	ps_subscriber_t *s = ps_new_subscriber(10, STRLIST("fun.inc"));
	PUB_BOOL_FL("thread.ready", true, FL_STICKY);
	ps_msg_t *msg = ps_get(s, 5000);
	assert(msg != NULL);
	assert(IS_INT(msg));
	PUB_INT(msg->rtopic, msg->int_val + 1); // Response
	ps_unref_msg(msg);
	ps_free_subscriber(s);
	return NULL;
}

void test_subscriptions(void) {
	printf("Test subscriptions\n");
	ps_subscriber_t *s1 = ps_new_subscriber(10, STRLIST("foo.bar"));
	ps_subscriber_t *s2 = ps_new_subscriber(10, STRLIST("foo", "baz"));
	assert(ps_num_subs(s1) == 1);
	assert(ps_num_subs(s2) == 2);
	ps_unsubscribe(s2, "baz");
	assert(ps_num_subs(s1) == 1);
	assert(ps_num_subs(s2) == 1);
	ps_free_subscriber(s1);
	ps_free_subscriber(s2);
	check_leak();
}

void test_subscribe_many(void) {
	printf("Test subscribe/unsubscribe many\n");
	ps_subscriber_t *s1 = ps_new_subscriber(10, NULL);
	assert(ps_subscribe_many(s1, STRLIST("foo", "bar", "baz")) == 3);
	assert(ps_num_subs(s1) == 3);
	assert(ps_unsubscribe_many(s1, STRLIST("foo", "bar", "baz")) == 3);
	assert(ps_num_subs(s1) == 0);
	ps_free_subscriber(s1);
	check_leak();
}

void test_publish(void) {
	printf("Test publish\n");
	ps_subscriber_t *s1 = ps_new_subscriber(10, STRLIST("foo.bar"));
	ps_subscriber_t *s2 = ps_new_subscriber(10, STRLIST("foo", "baz"));
	PUB_BOOL("foo.bar", true);
	PUB_BOOL("foo", true);
	assert(ps_waiting(s1) == 1);
	assert(ps_waiting(s2) == 2);
	assert(ps_stats_live_msg() == 2);
	ps_flush(s1);
	assert(ps_stats_live_msg() == 2);
	ps_flush(s2);
	assert(ps_stats_live_msg() == 0);
	assert(ps_waiting(s1) == 0);
	assert(ps_waiting(s2) == 0);
	ps_free_subscriber(s1);
	ps_free_subscriber(s2);
	check_leak();
}

void test_sticky(void) {
	printf("Test sticky\n");
	PUB_INT_FL("foo", 1, FL_STICKY);
	PUB_INT_FL("foo", 2, FL_STICKY); // Last sticky override previous
	ps_subscriber_t *s1 = ps_new_subscriber(10, STRLIST("foo"));
	assert(ps_waiting(s1) == 1);
	ps_msg_t *msg = ps_get(s1, -1);
	assert(msg->int_val == 2);
	ps_unref_msg(msg);
	ps_free_subscriber(s1);
	assert(ps_stats_live_msg() == 1); // The sticky message
	check_leak();
}

void test_no_recursive(void) {
	printf("Test no recursive\n");
	ps_subscriber_t *s1 = ps_new_subscriber(10, STRLIST("foo.bar"));
	ps_subscriber_t *s2 = ps_new_subscriber(10, STRLIST("foo"));
	PUB_INT_FL("foo.bar", 1, FL_NONRECURSIVE);
	assert(ps_waiting(s1) == 1);
	assert(ps_waiting(s2) == 0);
	ps_free_subscriber(s1);
	ps_free_subscriber(s2);
	check_leak();
}

void test_pub_get(void) {
	printf("Test pub->get\n");
	ps_msg_t *msg;
	ps_subscriber_t *s1 = ps_new_subscriber(10, STRLIST("foo.bar"));
	PUB_INT("foo.bar", 1);
	PUB_DBL("foo.bar", 1.25);
	PUB_STR("foo.bar", "Hello");
	PUB_ERR("foo.bar", -1, "Bad result");
	PUB_BUF("foo.bar", malloc(10), 10, free);

	msg = ps_get(s1, 10);
	assert(IS_INT(msg) && msg->int_val == 1);
	ps_unref_msg(msg);
	msg = ps_get(s1, 10);
	assert(IS_DBL(msg) && msg->dbl_val == 1.25);
	ps_unref_msg(msg);
	msg = ps_get(s1, 10);
	assert(IS_STR(msg) && strcmp(msg->str_val, "Hello") == 0);
	ps_unref_msg(msg);
	msg = ps_get(s1, 10);
	assert(IS_ERR(msg) && (msg->err_val.id == -1) && (strcmp(msg->err_val.desc, "Bad result") == 0));
	ps_unref_msg(msg);
	msg = ps_get(s1, 10);
	assert(IS_BUF(msg) && (msg->buf_val.sz == 10));
	ps_unref_msg(msg);

	msg = ps_get(s1, 1);
	assert(msg == NULL);

	assert(ps_waiting(s1) == 0);
	ps_free_subscriber(s1);
	check_leak();
}

void test_overflow(void) {
	printf("Test overflow\n");
	ps_msg_t *msg;
	ps_subscriber_t *s1 = ps_new_subscriber(2, STRLIST("foo.bar"));
	PUB_INT("foo.bar", 1);
	PUB_INT("foo.bar", 2);
	PUB_INT("foo.bar", 3);
	assert(ps_overflow(s1) == 1);
	assert(ps_overflow(s1) == 0);
	msg = ps_get(s1, 10);
	assert(IS_INT(msg) && msg->int_val == 1);
	ps_unref_msg(msg);
	msg = ps_get(s1, 10);
	assert(IS_INT(msg) && msg->int_val == 2);
	ps_unref_msg(msg);

	assert(ps_waiting(s1) == 0);
	ps_free_subscriber(s1);
	check_leak();
}

void test_call(void) {
	printf("Test call\n");
	ps_msg_t *msg = NULL;
	pthread_t thread;
	pthread_create(&thread, NULL, inc_thread, NULL);
	msg = ps_wait_one("thread.ready", 5000);
	assert(msg != NULL && msg->bool_val == true);
	ps_unref_msg(msg);
	msg = CALL_INT("fun.inc", 25, 1000);
	assert(msg != NULL && msg->int_val == 26);
	ps_unref_msg(msg);
	pthread_join(thread, NULL);
	check_leak();
}

void test_no_return_path(void) {
	printf("Test no return path\n");
	ps_msg_t *msg = NULL;
	pthread_t thread;
	pthread_create(&thread, NULL, inc_thread, NULL);
	msg = ps_wait_one("thread.ready", 5000);
	assert(msg != NULL && msg->bool_val == true);
	ps_unref_msg(msg);
	PUB_INT("fun.inc", 25);
	pthread_join(thread, NULL);
	check_leak();
}

void run_all(void) {
	test_subscriptions();
	test_subscribe_many();
	test_publish();
	test_sticky();
	test_no_recursive();
	test_pub_get();
	test_overflow();
	test_call();
	test_no_return_path();
	printf("All tests passed!\n");
}

int main(void) {
	ps_init();
	run_all();
	return 0;
}
