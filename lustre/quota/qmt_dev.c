// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2012, 2017, Intel Corporation.
 * Use is subject to license terms.
 */

/*
 * Management of the device associated with a Quota Master Target (QMT).
 *
 * The QMT holds the cluster wide quota limits. It stores the quota settings
 * ({hard,soft} limit & grace time) in a global index file and is in charge
 * of allocating quota space to slaves while guaranteeing that the overall
 * limits aren't exceeded. The QMT also maintains one index per slave (in fact,
 * one per slave per quota type) used to track how much space is allocated
 * to a given slave. Now that the QMT is aware of the quota space distribution
 * among slaves, it can afford to rebalance efficiently quota space from one
 * slave to another. Slaves are asked to release quota space via glimpse
 * callbacks sent on DLM locks which are granted to slaves when those latters
 * acquire quota space.
 *
 * The QMT device is currently set up by the MDT and should probably be moved
 * to a separate target in the future. Meanwhile, the MDT forwards all quota
 * requests to the QMT via a list of request handlers (see struct qmt_handlers
 * in lustre_quota.h). The QMT also borrows the LDLM namespace from the MDT.
 *
 * To bring up a QMT device, the following steps must be completed:
 *
 * - call ->ldto_device_alloc to allocate the QMT device and perform basic
 *   initialization like connecting to the backend OSD device or setting up the
 *   default pools and the QMT procfs directory.
 *
 * - the MDT can then connect to the QMT instance via legacy obd_connect path.
 *
 * - once the MDT stack has been fully configured, ->ldto_prepare must be called
 *   to configure on-disk objects associated with this master target.
 *
 * To shutdown a QMT device, the MDT just has to disconnect from the QMT.
 *
 * The qmt_device_type structure is registered when the lquota module is
 * loaded and all the steps described above are automatically done when the MDT
 * set up the Quota Master Target via calls to class_attach/class_setup, see
 * mdt_quota_init() for more details.
 *
 * Author: Johann Lombardi <johann.lombardi@intel.com>
 * Author: Niu    Yawei    <yawei.niu@intel.com>
 */

#define DEBUG_SUBSYSTEM S_LQUOTA

#include <obd_class.h>
#include <lprocfs_status.h>
#include <lustre_disk.h>
#include "qmt_internal.h"

static const struct lu_device_operations qmt_lu_ops;

/*
 * Release quota master target and all data structure associated with this
 * target.
 * Called on MDT0 cleanup.
 *
 * \param env - is the environment passed by the caller
 * \param ld  - is the lu_device associated with the qmt device to be released
 *
 * \retval - NULL on success (backend OSD device is managed by the main stack),
 *           appropriate error on failure
 */
static struct lu_device *qmt_device_fini(const struct lu_env *env,
					 struct lu_device *ld)
{
	struct qmt_device	*qmt = lu2qmt_dev(ld);
	ENTRY;

	LASSERT(qmt != NULL);

	CDEBUG(D_QUOTA, "%s: initiating QMT shutdown\n", qmt->qmt_svname);
	qmt->qmt_stopping = true;

	if (qmt->qmt_lvbo_free_wq) {
		destroy_workqueue(qmt->qmt_lvbo_free_wq);
		qmt->qmt_lvbo_free_wq = NULL;
	}

	/* kill pool instances, if any */
	qmt_pool_fini(env, qmt);

	/* remove qmt proc entry */
	if (qmt->qmt_proc != NULL && !IS_ERR(qmt->qmt_proc)) {
		lprocfs_remove(&qmt->qmt_proc);
		qmt->qmt_proc = NULL;
	}

	/* stop rebalance thread */
	if (!qmt->qmt_child->dd_rdonly)
		qmt_stop_reba_thread(qmt);

	if (qmt->qmt_root) {
		dt_object_put(env, qmt->qmt_root);
		qmt->qmt_root = NULL;
	}

	/* disconnect from OSD */
	if (qmt->qmt_child_exp != NULL) {
		obd_disconnect(qmt->qmt_child_exp);
		qmt->qmt_child_exp = NULL;
		qmt->qmt_child = NULL;
	}

	/* clear references to MDT namespace */
	ld->ld_obd->obd_namespace = NULL;
	qmt->qmt_ns = NULL;

	RETURN(NULL);
}

/*
 * Connect a quota master to the backend OSD device.
 *
 * \param env - is the environment passed by the caller
 * \param qmt - is the quota master target to be connected
 * \param cfg - is the configuration log record from which we need to extract
 *              the service name of the backend OSD device to connect to.
 *
 * \retval - 0 on success, appropriate error on failure
 */
static int qmt_connect_to_osd(const struct lu_env *env, struct qmt_device *qmt,
			      struct lustre_cfg *cfg)
{
	struct obd_connect_data	*data = NULL;
	struct obd_device	*obd;
	struct lu_device	*ld = qmt2lu_dev(qmt);
	int			 rc;
	ENTRY;

	LASSERT(qmt->qmt_child_exp == NULL);

	OBD_ALLOC_PTR(data);
	if (data == NULL)
		GOTO(out, rc = -ENOMEM);

	/* look-up OBD device associated with the backend OSD device.
	 * The MDT is kind enough to pass the OBD name in QMT configuration */
	obd = class_name2obd(lustre_cfg_string(cfg, 3));
	if (obd == NULL) {
		CERROR("%s: can't locate backend osd device: %s\n",
		       qmt->qmt_svname, lustre_cfg_string(cfg, 3));
		GOTO(out, rc = -ENOTCONN);
	}

	data->ocd_connect_flags = OBD_CONNECT_VERSION;
	data->ocd_version = LUSTRE_VERSION_CODE;

	/* connect to OSD device */
	rc = obd_connect(NULL, &qmt->qmt_child_exp, obd, &obd->obd_uuid, data,
			 NULL);
	if (rc) {
		CERROR("%s: cannot connect to osd dev %s (%d)\n",
		       qmt->qmt_svname, obd->obd_name, rc);
		GOTO(out, rc);
	}

	/* initialize site (although it isn't used anywhere) and lu_device
	 * pointer to next device */
	qmt->qmt_child = lu2dt_dev(qmt->qmt_child_exp->exp_obd->obd_lu_dev);
	ld->ld_site = qmt->qmt_child_exp->exp_obd->obd_lu_dev->ld_site;
	EXIT;
out:
	OBD_FREE_PTR(data);
	return rc;
}

/*
 * Initialize quota master target device. This includers connecting to
 * the backend OSD device, initializing the pool configuration and creating the
 * root procfs directory dedicated to this quota target.
 * The rest of the initialization is done when the stack is fully configured
 * (i.e. when ->ldo_start is called across the stack).
 *
 * This function is called on MDT0 setup.
 *
 * \param env - is the environment passed by the caller
 * \param qmt - is the quota master target to be initialized
 * \param ldt - is the device type structure associated with the qmt device
 * \param cfg - is the configuration record used to configure the qmt device
 *
 * \retval - 0 on success, appropriate error on failure
 */
static int qmt_device_init0(const struct lu_env *env, struct qmt_device *qmt,
			    struct lu_device_type *ldt, struct lustre_cfg *cfg)
{
	struct lu_device	*ld = qmt2lu_dev(qmt);
	struct obd_device	*obd, *mdt_obd;
	struct obd_type		*type;
	char			*svname = lustre_cfg_string(cfg, 0);
	int			 rc;
	ENTRY;

	if (svname == NULL)
		RETURN(-EINVAL);

	/* record who i am, it might be useful ... */
	rc = strscpy(qmt->qmt_svname, svname, sizeof(qmt->qmt_svname));
	if (rc < 0)
		RETURN(rc);

	/* look-up the obd_device associated with the qmt */
	obd = class_name2obd(qmt->qmt_svname);
	if (obd == NULL)
		RETURN(-ENOENT);

	/* reference each other */
	obd->obd_lu_dev = ld;
	ld->ld_obd      = obd;

	/* look-up the parent MDT to steal its ldlm namespace ... */
	mdt_obd = class_name2obd(lustre_cfg_string(cfg, 2));
	if (mdt_obd == NULL)
		RETURN(-ENOENT);

	/* borrow  MDT namespace. kind of a hack until we have our own namespace
	 * & service threads */
	LASSERT(mdt_obd->obd_namespace != NULL);
	obd->obd_namespace = mdt_obd->obd_namespace;
	qmt->qmt_ns = obd->obd_namespace;

	/* connect to backend osd device */
	rc = qmt_connect_to_osd(env, qmt, cfg);
	if (rc)
		GOTO(out, rc);

	/* set up and start rebalance thread */
	INIT_LIST_HEAD(&qmt->qmt_reba_list);
	spin_lock_init(&qmt->qmt_reba_lock);
	if (!qmt->qmt_child->dd_rdonly) {
		rc = qmt_start_reba_thread(qmt);
		if (rc) {
			CERROR("%s: failed to start rebalance thread (%d)\n",
			       qmt->qmt_svname, rc);
			GOTO(out, rc);
		}
	}

	/* at the moment there is no linkage between lu_type and obd_type, so
	 * we lookup obd_type this way */
	type = class_search_type(LUSTRE_QMT_NAME);
	LASSERT(type != NULL);

	/* put reference taken by class_search_type */
	kobject_put(&type->typ_kobj);

	/* register proc directory associated with this qmt */
	qmt->qmt_proc = lprocfs_register(qmt->qmt_svname, type->typ_procroot,
					 NULL, NULL);
	if (IS_ERR(qmt->qmt_proc)) {
		rc = PTR_ERR(qmt->qmt_proc);
		CERROR("%s: failed to create qmt proc entry (%d)\n",
		       qmt->qmt_svname, rc);
		GOTO(out, rc);
	}

	/* initialize pool configuration */
	rc = qmt_pool_init(env, qmt);
	if (rc)
		GOTO(out, rc);

	qmt->qmt_lvbo_free_wq = alloc_workqueue("qmt_lvbo_free", WQ_UNBOUND, 0);
	if (!qmt->qmt_lvbo_free_wq) {
		rc = -ENOMEM;
		CERROR("%s: failed to start qmt_lvbo_free workqueue: rc = %d\n",
		       qmt->qmt_svname, rc);
		goto out;
	}

	EXIT;
out:
	if (rc)
		qmt_device_fini(env, ld);
	return rc;
}

/*
 * Free quota master target device. Companion of qmt_device_alloc()
 *
 * \param env - is the environment passed by the caller
 * \param ld  - is the lu_device associated with the qmt dev to be freed
 *
 * \retval - NULL on success (backend OSD device is managed by the main stack),
 *           appropriate error on failure
 */
static struct lu_device *qmt_device_free(const struct lu_env *env,
					 struct lu_device *ld)
{
	struct qmt_device	*qmt = lu2qmt_dev(ld);
	ENTRY;

	LASSERT(qmt != NULL);

	lu_device_fini(ld);
	OBD_FREE_PTR(qmt);
	RETURN(NULL);
}

/*
 * Allocate quota master target and initialize it.
 *
 * \param env - is the environment passed by the caller
 * \param ldt - is the device type structure associated with the qmt
 * \param cfg - is the configuration record used to configure the qmt
 *
 * \retval - lu_device structure associated with the qmt on success,
 *           appropriate error on failure
 */
static struct lu_device *qmt_device_alloc(const struct lu_env *env,
					  struct lu_device_type *ldt,
					  struct lustre_cfg *cfg)
{
	struct qmt_device	*qmt;
	struct lu_device	*ld;
	int			 rc;
	ENTRY;

	/* allocate qmt device */
	OBD_ALLOC_PTR(qmt);
	if (qmt == NULL)
		RETURN(ERR_PTR(-ENOMEM));

	/* configure lu/dt_device */
	ld = qmt2lu_dev(qmt);
	dt_device_init(&qmt->qmt_dt_dev, ldt);
	ld->ld_ops = &qmt_lu_ops;

	/* initialize qmt device */
	rc = qmt_device_init0(env, qmt, ldt, cfg);
	if (rc != 0) {
		qmt_device_free(env, ld);
		RETURN(ERR_PTR(rc));
	}

	RETURN(ld);
}

LU_KEY_INIT_FINI(qmt, struct qmt_thread_info);
LU_TYPE_INIT_FINI(qmt, &qmt_thread_key);
LU_CONTEXT_KEY_DEFINE(qmt, LCT_MD_THREAD);

/*
 * lu device type operations associated with the master target.
 */
static const struct lu_device_type_operations qmt_device_type_ops = {
	.ldto_init		= qmt_type_init,
	.ldto_fini		= qmt_type_fini,

	.ldto_start		= qmt_type_start,
	.ldto_stop		= qmt_type_stop,

	.ldto_device_alloc	= qmt_device_alloc,
	.ldto_device_free	= qmt_device_free,

	.ldto_device_fini	= qmt_device_fini,
};

/*
 * lu device type structure associated with the master target.
 * MDT0 uses this structure to configure the qmt.
 */
static struct lu_device_type qmt_device_type = {
	.ldt_tags	= LU_DEVICE_DT,
	.ldt_name	= LUSTRE_QMT_NAME,
	.ldt_ops	= &qmt_device_type_ops,
	.ldt_ctx_tags	= LCT_MD_THREAD,
};

/*
 * obd_connect handler used by the MDT to connect to the master target.
 */
static int qmt_device_obd_connect(const struct lu_env *env,
				  struct obd_export **exp,
				  struct obd_device *obd,
				  struct obd_uuid *cluuid,
				  struct obd_connect_data *data,
				  void *localdata)
{
	struct lustre_handle	conn;
	int			rc;
	ENTRY;

	rc = class_connect(&conn, obd, cluuid);
	if (rc)
		RETURN(rc);

	*exp = class_conn2export(&conn);
	RETURN(0);
}

/*
 * obd_disconnect handler used by the MDT to disconnect from the master target.
 * We trigger cleanup on disconnect since it means that the MDT is about to
 * shutdown.
 */
static int qmt_device_obd_disconnect(struct obd_export *exp)
{
	struct obd_device	*obd = exp->exp_obd;
	int			 rc;
	ENTRY;

	rc = class_disconnect(exp);
	if (rc)
		RETURN(rc);

	rc = class_manual_cleanup(obd);
	RETURN(0);
}

/*
 * obd device operations associated with the master target.
 */
static const struct obd_ops qmt_obd_ops = {
	.o_owner	= THIS_MODULE,
	.o_connect	= qmt_device_obd_connect,
	.o_disconnect	= qmt_device_obd_disconnect,
	.o_pool_new	= qmt_pool_new,
	.o_pool_rem	= qmt_pool_rem,
	.o_pool_add	= qmt_pool_add,
	.o_pool_del	= qmt_pool_del,
};

/*
 * Called when the MDS is fully configured. We use it to set up local objects
 * associated with the quota master target.
 *
 * \param env - is the environment passed by the caller
 * \param parent - is the lu_device of the parent, that's to say the mdt
 * \param ld  - is the lu_device associated with the master target
 *
 * \retval    - 0 on success, appropriate error on failure
 */
static int qmt_device_prepare(const struct lu_env *env,
			      struct lu_device *parent,
			      struct lu_device *ld)
{
	struct qmt_device	*qmt = lu2qmt_dev(ld);
	struct dt_object	*qmt_root;
	int			 rc;
	ENTRY;

	/* initialize quota master root directory where all index files will be
	 * stored */
	qmt_root = lquota_disk_dir_find_create(env, qmt->qmt_child, NULL,
					       QMT_DIR);
	if (IS_ERR(qmt_root)) {
		rc = PTR_ERR(qmt_root);
		CERROR("%s: failed to create master quota directory (%d)\n",
		       qmt->qmt_svname, rc);
		RETURN(rc);
	}

	qmt->qmt_root = qmt_root;
	/* initialize on-disk indexes associated with each pool */
	rc = qmt_pool_prepare(env, qmt, qmt_root, NULL);
	RETURN(rc);
}

/*
 * lu device operations for the quota master target
 */
static const struct lu_device_operations qmt_lu_ops = {
	.ldo_prepare		= qmt_device_prepare,
	.ldo_process_config	= NULL, /* to be defined for dynamic pool
					 * configuration */
};

/* global variable initialization called when the lquota module is loaded */
int qmt_glb_init(void)
{
	int rc;
	ENTRY;

	rc = class_register_type(&qmt_obd_ops, NULL, true,
				 LUSTRE_QMT_NAME, &qmt_device_type);
	RETURN(rc);
}

/* called when the lquota module is about to be unloaded */
void qmt_glb_fini(void)
{
	class_unregister_type(LUSTRE_QMT_NAME);
}
