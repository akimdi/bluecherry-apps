/*
 * Copyright (C) 2010 Bluecherry, LLC
 *
 * Confidential, all rights reserved. No distribution is permitted.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>

#include "libbluecherry.h"
extern "C" {
#include "php.h"
#include "ext/standard/php_standard.h"
}

static char bch_name[] = "BC Handle";

#define BCDB_NAME	"BC-DB Handle"

static int bch_id;

static void bch_destructor(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	struct bc_handle *bch = (struct bc_handle *)rsrc->ptr;

	if (bch)
		bc_handle_free(bch);

	return;
}

PHP_MINIT_FUNCTION(bluecherry)
{
	bch_id = zend_register_list_destructors_ex(bch_destructor, NULL,
						   bch_name, module_number);

	REGISTER_LONG_CONSTANT("BC_CID_HUE", V4L2_CID_HUE, 0);
	REGISTER_LONG_CONSTANT("BC_CID_CONTRAST", V4L2_CID_CONTRAST, 0);
	REGISTER_LONG_CONSTANT("BC_CID_BRIGHTNESS", V4L2_CID_BRIGHTNESS, 0);
	REGISTER_LONG_CONSTANT("BC_CID_SATURATION", V4L2_CID_SATURATION, 0);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(bluecherry)
{
	return SUCCESS;
}

PHP_MINFO_FUNCTION(bluecherry)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Bluecherry library support", "enabled");
	php_info_print_table_end();

	return;
}

#define BCH_GET_RES(__func) do {					\
	zval *z_ctx;							\
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r",	\
				  &z_ctx) == FAILURE)			\
		RETURN_FALSE;						\
	ZEND_FETCH_RESOURCE(bch, struct bc_handle *, &z_ctx, -1,	\
			    bch_name, bch_id);				\
	if (bch == NULL) {						\
		php_error_docref(NULL TSRMLS_CC, E_WARNING,		\
			__func ": invalid context");			\
		RETURN_FALSE;						\
	}								\
} while(0)

PHP_FUNCTION(bc_db_open)
{
	if (bc_db_open())
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_db_close)
{
	bc_db_close();
	RETURN_TRUE;
}

PHP_FUNCTION(bc_db_escape_string)
{
	zval *z_ctx;
	char *str;
	int str_len;
	char *tmp_str, *tmp_str_cpy;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
				  &str, &str_len) == FAILURE)
		RETURN_FALSE;

	if (bc_db_open())
		RETURN_FALSE;

	str[str_len] = '\0';
	tmp_str = bc_db_escape_string(str, str_len);
	if (tmp_str == NULL)
		RETURN_FALSE;

	/* All returned values must be allocated by emalloc, so we must
	 * copy the result. */
	tmp_str_cpy = estrdup(tmp_str);
	free(tmp_str);
	RETURN_STRING(tmp_str_cpy, 0);
}

PHP_FUNCTION(bc_db_query)
{
	zval *z_ctx;
	char *sql;
	int sql_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
				  &sql, &sql_len) == FAILURE)
		RETURN_FALSE;

	if (bc_db_query("%s", sql))
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_db_last_insert_rowid)
{
	unsigned long id = bc_db_last_insert_rowid();
	RETURN_LONG(id);
}

PHP_FUNCTION(bc_db_get_table)
{
	zval *z_ctx;
	BC_DB_RES dbres;
	char *sql;
	int sql_len;
	int ncols;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
				  &sql, &sql_len) == FAILURE)
		RETURN_FALSE;

	if (bc_db_open())
		RETURN_FALSE;

	dbres = bc_db_get_table("%s", sql);
	if (dbres == NULL)
		RETURN_FALSE;

	ncols = bc_db_num_fields(dbres);
	if (ncols < 0) {
		bc_db_free_table(dbres);
		RETURN_FALSE;
	}

	array_init(return_value);

	while (!bc_db_fetch_row(dbres)) {
		zval *row_arr;
		int i;

		ALLOC_INIT_ZVAL(row_arr);
		array_init(row_arr);

		for (i = 0; i < ncols; i++) {
			const char *field = bc_db_get_field(dbres, i);
			const char *str;
			int len;

			/* This would be an error actually */
			if (field == NULL)
				continue;

			str = bc_db_get_val(dbres, field, &len);
			if (str == NULL)
				add_assoc_null(row_arr, field);
			else
				add_assoc_stringl(row_arr, field,
						 (char *)str, len, 1);
		}

		add_next_index_zval(return_value, row_arr);
	}

	bc_db_free_table(dbres);
}

/*
 * Command is a string of single characters:
 *    (S)top
 *    Move: (u)p (d)own (l)eft (r)ight (w)ide (t)ight
 *    Presets: (s)ave (g)o (c)lear
 *
 * Stop can only be by itself (capital S). Move commands and Preset commands
 * cannot be used together. Some Move commands are mutually exclusive (e.g.
 * left and right) and all Preset commands are mutually exclusive.
 */
PHP_FUNCTION(bc_ptz_cmd)
{
	struct bc_handle bch;
	long id, delay, pan_speed, tilt_speed, pset_id;
	char *cmd_str;
	int cmd_len, ret, i;
	unsigned int cmd;
	BC_DB_RES dbres;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lsllll", &id,
				  &cmd_str, &cmd_len, &delay, &pan_speed,
				  &tilt_speed, &pset_id) == FAILURE)
		RETURN_FALSE;

	if (id < 0)
		RETURN_FALSE;

	if (bc_db_open())
		RETURN_FALSE;

	dbres = bc_db_get_table("SELECT * FROM Devices LEFT OUTER JOIN "
				"AvailableSources USING (device) WHERE "
				"Devices.id=%ld AND disabled=0", id);

	if (dbres == NULL)
		RETURN_FALSE;

	if (bc_db_fetch_row(dbres)) {
		bc_db_free_table(dbres);
		RETURN_FALSE;
	}

	memset(&bch, 0, sizeof(bch));
	bc_ptz_check(&bch, dbres);

	for (i = 0, cmd = 0; i < cmd_len; i++) {
		switch (cmd_str[i]) {
		case 'S': cmd |= BC_PTZ_CMD_STOP; break;
		case 'r': cmd |= BC_PTZ_CMD_RIGHT; break;
		case 'l': cmd |= BC_PTZ_CMD_LEFT; break;
		case 'u': cmd |= BC_PTZ_CMD_UP; break;
		case 'd': cmd |= BC_PTZ_CMD_DOWN; break;
		case 't': cmd |= BC_PTZ_CMD_IN; break;
		case 'w': cmd |= BC_PTZ_CMD_OUT; break;
		case 's': cmd |= BC_PTZ_CMD_SAVE; break;
		case 'g': cmd |= BC_PTZ_CMD_GO; break;
		case 'c': cmd |= BC_PTZ_CMD_CLEAR; break;
		default: RETURN_FALSE;
		}
	}

	ret = bc_ptz_cmd(&bch, cmd, (int)delay, (int)pan_speed,
			 (int)tilt_speed, (int)pset_id);

	bc_db_free_table(dbres);

	if (ret)
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_handle_get_byid)
{
	struct bc_handle *bch;
	long id;
	BC_DB_RES dbres;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &id) == FAILURE)
		RETURN_FALSE;

	if (id < 0)
		RETURN_FALSE;

	if (bc_db_open())
		RETURN_FALSE;

	dbres = bc_db_get_table("SELECT * FROM Devices LEFT OUTER JOIN "
				"AvailableSources USING (device) WHERE "
				"Devices.id=%ld AND disabled=0", id);

	if (dbres == NULL)
		RETURN_FALSE;

	if (bc_db_fetch_row(dbres)) {
		bc_db_free_table(dbres);
		RETURN_FALSE;
	}

	bch = bc_handle_get(dbres);
	bc_db_free_table(dbres);

	if (bch == NULL)
		RETURN_FALSE;

	ZEND_REGISTER_RESOURCE(return_value, bch, bch_id);
}

PHP_FUNCTION(bc_handle_get)
{
	struct bc_handle *bch;
	char *devname;
	char *driver;
	int devname_len, driver_len;
	BC_DB_RES dbres;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &devname,
				  &devname_len, &driver, &driver_len) == FAILURE)
		RETURN_FALSE;

	if (bc_db_open())
		RETURN_FALSE;

	dbres = bc_db_get_table("SELECT * FROM Devices LEFT OUTER JOIN "
				"AvailableSources USING (device) WHERE "
				"Devices.device='%s' AND Devices.driver='%s' "
				"AND disabled=0",
				devname, driver);

	if (dbres == NULL)
		RETURN_FALSE;

	if (bc_db_fetch_row(dbres)) {
		bc_db_free_table(dbres);
		RETURN_FALSE;
	}

	bch = bc_handle_get(dbres);
	bc_db_free_table(dbres);

	if (bch == NULL)
		RETURN_FALSE;

	ZEND_REGISTER_RESOURCE(return_value, bch, bch_id);
}

PHP_FUNCTION(bc_set_control)
{
	zval *z_ctx;
	struct bc_handle *bch;
	long ctrl, val;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &z_ctx,
				  &ctrl, &val) == FAILURE)
		RETURN_FALSE;
	ZEND_FETCH_RESOURCE(bch, struct bc_handle *, &z_ctx, -1,
			    bch_name, bch_id);
	if (bch == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING,
				 "bc_set_control: invalid context");
		RETURN_FALSE;
	}

	if (bc_set_control(bch, ctrl, val))
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_handle_free)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_handle_free");

	zend_list_delete((long)bch);

	RETURN_TRUE;
}

PHP_FUNCTION(bc_handle_start)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_handle_start");

	if (bc_handle_start(bch, NULL) < 0)
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_buf_get)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_buf_get");

	if (bc_buf_get(bch))
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_buf_size)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_buf_size");

	RETURN_LONG(bc_buf_size(bch));
}

PHP_FUNCTION(bc_buf_data)
{
	struct bc_handle *bch;
        unsigned char *data;
	int size;

	BCH_GET_RES("bc_buf_data");

	data = (unsigned char*)bc_buf_data(bch);
	size = bc_buf_size(bch);
	if (data == NULL || size <= 0)
		RETURN_FALSE;

	RETURN_STRINGL((const char*)data, size, 1);
}

PHP_FUNCTION(bc_set_mjpeg)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_set_mjpeg");

	if (bc_set_mjpeg(bch))
		RETURN_FALSE;

	RETURN_TRUE;
}

PHP_FUNCTION(bc_get_mjpeg_url)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_get_mjpeg_url");

	if (!(bch->cam_caps & BC_CAM_CAP_MJPEG_URL))
		RETURN_FALSE;

	RETURN_STRING(bch->mjpeg_url, 1);
}

PHP_FUNCTION(bc_log)
{
	char *str;
	int str_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
				  &str, &str_len) == FAILURE)
		RETURN_FALSE;

	bc_log("%s", str);

	RETURN_TRUE;
}

PHP_FUNCTION(bc_motion_is_on)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_motion_is_on");

	if (bc_motion_is_on(bch))
		RETURN_TRUE;

	RETURN_FALSE;
}

PHP_FUNCTION(bc_motion_is_detected)
{
	struct bc_handle *bch;

	BCH_GET_RES("bc_motion_is_detected");

	if (bc_motion_is_detected(bch))
		RETURN_TRUE;

	RETURN_FALSE;
}

static function_entry bluecherry_functions[] = {
	/* Bluecherry DB Handlers */
	PHP_FE(bc_db_open, NULL)
	PHP_FE(bc_db_close, NULL)
	PHP_FE(bc_db_get_table, NULL)
	PHP_FE(bc_db_query, NULL)
	PHP_FE(bc_db_last_insert_rowid, NULL)
	PHP_FE(bc_db_escape_string, NULL)
	/* Bluecherry Video Handlers */
	PHP_FE(bc_handle_get, NULL)
	PHP_FE(bc_handle_get_byid, NULL)
	PHP_FE(bc_handle_free, NULL)
	PHP_FE(bc_handle_start, NULL)
	PHP_FE(bc_buf_get, NULL)
	PHP_FE(bc_buf_size, NULL)
	PHP_FE(bc_buf_data, NULL)
	PHP_FE(bc_set_mjpeg, NULL)
	PHP_FE(bc_set_control, NULL)
	PHP_FE(bc_get_mjpeg_url, NULL)
	PHP_FE(bc_motion_is_on, NULL)
	PHP_FE(bc_motion_is_detected, NULL)
	PHP_FE(bc_ptz_cmd, NULL)
	/* Miscellaneous */
	PHP_FE(bc_log, NULL)
	{NULL, NULL, NULL}
};

zend_module_entry bluecherry_module_entry = {
	STANDARD_MODULE_HEADER,
	"bluecherry",
	bluecherry_functions,
	PHP_MINIT(bluecherry),
	PHP_MSHUTDOWN(bluecherry),
	NULL,
	NULL,
	PHP_MINFO(bluecherry),
	"1.0",
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_BLUECHERRY
ZEND_GET_MODULE(bluecherry)
#endif