 * ----------	---------- 	----------	----------
	if (uap->fname == NULL)
	if (uap->fname == NULL)
	if (fd == AT_FDCWD || uap->fname != NULL)	/* same as chmod() */
	if (uap->fnamep == NULL)
{	/* AUS_START */
		if (copyin((caddr_t)dataptr,
				STRUCT_BUF(nfsargs),
				MIN(uap->datalen, STRUCT_SIZE(nfsargs)))) {
				(caddr_t)hostname,
				MAXNAMELEN, &len)) {
			(uint_t)STRUCT_FGET(nfsargs, flags)));
	case 4: /* getpgid() 	- not security relevant */
			au_uwrite(au_to_arg32(3, "arg", (uint32_t)cmarg));
			au_uwrite(au_to_arg64(3, "arg", (uint64_t)cmarg));
		au_uwrite(au_to_arg32(3, "arg", (uint32_t)cmarg));
		au_uwrite(au_to_arg64(3, "arg", (uint64_t)cmarg));
			e = AUE_NULL;
			if (uap->addr == NULL)
aus_sockconfig(tad)
	struct t_audit_data *tad;
			if (uap->from == NULL)
			if (uap->to == NULL)
auf_read(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;
auf_write(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;
auf_recv(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;
auf_send(tad, error, rval)
	struct t_audit_data *tad;
	int error;
	rval_t *rval;