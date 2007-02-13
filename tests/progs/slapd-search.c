/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1999-2007 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
/* ACKNOWLEDGEMENTS:
 * This work was initially developed by Kurt Spanier for inclusion
 * in OpenLDAP Software.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/stdlib.h>

#include <ac/ctype.h>
#include <ac/param.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/unistd.h>
#include <ac/wait.h>

#include <ldap.h>
#include <lutil.h>

#include "slapd-common.h"

#define LOOPS	100
#define RETRIES	0

static void
do_search( char *uri, char *manager, struct berval *passwd,
	char *sbase, char *filter, LDAP **ldp, int noattrs, int nobind,
	int innerloop, int maxretries, int delay, int force, int chaserefs );

static void
do_random( char *uri, char *manager, struct berval *passwd,
	char *sbase, char *filter, char *attr, int noattrs, int nobind,
	int innerloop, int maxretries, int delay, int force, int chaserefs );

static void
usage( char *name )
{
        fprintf( stderr,
		"usage: %s "
		"-H <uri> | ([-h <host>] -p <port>) "
		"-D <manager> "
		"-w <passwd> "
		"-b <searchbase> "
		"-f <searchfilter> "
		"[-a <attr>] "
		"[-A] "
		"[-C] "
		"[-F] "
		"[-N] "
		"[-i <ignore>] "
		"[-l <loops>] "
		"[-L <outerloops>] "
		"[-r <maxretries>] "
		"[-t <delay>]\n",
			name );
	exit( EXIT_FAILURE );
}

int
main( int argc, char **argv )
{
	int		i;
	char		*uri = NULL;
	char		*host = "localhost";
	int		port = -1;
	char		*manager = NULL;
	struct berval	passwd = { 0, NULL };
	char		*sbase = NULL;
	char		*filter  = NULL;
	char		*attr = NULL;
	int		loops = LOOPS;
	int		outerloops = 1;
	int		retries = RETRIES;
	int		delay = 0;
	int		force = 0;
	int		chaserefs = 0;
	int		noattrs = 0;
	int		nobind = 0;

	tester_init( "slapd-search", TESTER_SEARCH );

	/* by default, tolerate referrals and no such object */
	tester_ignore_str2errlist( "REFERRAL,NO_SUCH_OBJECT" );

	while ( ( i = getopt( argc, argv, "Aa:b:CD:f:FH:h:i:l:L:Np:r:t:w:" ) ) != EOF )
	{
		switch ( i ) {
		case 'A':
			noattrs++;
			break;

		case 'C':
			chaserefs++;
			break;

		case 'H':		/* the server uri */
			uri = strdup( optarg );
			break;

		case 'h':		/* the servers host */
			host = strdup( optarg );
			break;

		case 'i':
			tester_ignore_str2errlist( optarg );
			break;

		case 'N':
			nobind++;
			break;

		case 'p':		/* the servers port */
			if ( lutil_atoi( &port, optarg ) != 0 ) {
				usage( argv[0] );
			}
			break;

		case 'D':		/* the servers manager */
			manager = strdup( optarg );
			break;

		case 'w':		/* the server managers password */
			passwd.bv_val = strdup( optarg );
			passwd.bv_len = strlen( optarg );
			memset( optarg, '*', passwd.bv_len );
			break;

		case 'a':
			attr = strdup( optarg );
			break;

		case 'b':		/* file with search base */
			sbase = strdup( optarg );
			break;

		case 'f':		/* the search request */
			filter = strdup( optarg );
			break;

		case 'F':
			force++;
			break;

		case 'l':		/* number of loops */
			if ( lutil_atoi( &loops, optarg ) != 0 ) {
				usage( argv[0] );
			}
			break;

		case 'L':		/* number of loops */
			if ( lutil_atoi( &outerloops, optarg ) != 0 ) {
				usage( argv[0] );
			}
			break;

		case 'r':		/* number of retries */
			if ( lutil_atoi( &retries, optarg ) != 0 ) {
				usage( argv[0] );
			}
			break;

		case 't':		/* delay in seconds */
			if ( lutil_atoi( &delay, optarg ) != 0 ) {
				usage( argv[0] );
			}
			break;

		default:
			usage( argv[0] );
			break;
		}
	}

	if (( sbase == NULL ) || ( filter == NULL ) || ( port == -1 && uri == NULL ))
		usage( argv[0] );

	if ( *filter == '\0' ) {

		fprintf( stderr, "%s: invalid EMPTY search filter.\n",
				argv[0] );
		exit( EXIT_FAILURE );

	}

	uri = tester_uri( uri, host, port );

	for ( i = 0; i < outerloops; i++ ) {
		if ( attr != NULL ) {
			do_random( uri, manager, &passwd, sbase, filter, attr,
				noattrs, nobind, loops, retries, delay, force, chaserefs );

		} else {
			do_search( uri, manager, &passwd, sbase, filter, NULL,
				noattrs, nobind, loops, retries, delay, force, chaserefs );
		}
	}

	exit( EXIT_SUCCESS );
}


static void
do_random( char *uri, char *manager, struct berval *passwd,
	char *sbase, char *filter, char *attr, int noattrs, int nobind,
	int innerloop, int maxretries, int delay, int force, int chaserefs )
{
	LDAP	*ld = NULL;
	int  	i = 0, do_retry = maxretries;
	char	*attrs[ 2 ];
	int     rc = LDAP_SUCCESS;
	int	version = LDAP_VERSION3;
	int	nvalues = 0;
	char	**values = NULL;
	LDAPMessage *res = NULL, *e = NULL;

	attrs[ 0 ] = attr;
	attrs[ 1 ] = NULL;

	ldap_initialize( &ld, uri );
	if ( ld == NULL ) {
		tester_perror( "ldap_initialize", NULL );
		exit( EXIT_FAILURE );
	}

	(void) ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version ); 
	(void) ldap_set_option( ld, LDAP_OPT_REFERRALS,
		chaserefs ? LDAP_OPT_ON : LDAP_OPT_OFF );

	if ( do_retry == maxretries ) {
		fprintf( stderr, "PID=%ld - Search(%d): base=\"%s\", filter=\"%s\" attr=\"%s\".\n",
				(long) pid, innerloop, sbase, filter, attr );
	}

	if ( nobind == 0 ) {
		rc = ldap_sasl_bind_s( ld, manager, LDAP_SASL_SIMPLE, passwd, NULL, NULL, NULL );
		if ( rc != LDAP_SUCCESS ) {
			tester_ldap_error( ld, "ldap_sasl_bind_s", NULL );
			switch ( rc ) {
			case LDAP_BUSY:
			case LDAP_UNAVAILABLE:
			/* fallthru */
			default:
				break;
			}
			exit( EXIT_FAILURE );
		}
	}

	rc = ldap_search_ext_s( ld, sbase, LDAP_SCOPE_SUBTREE,
		filter, attrs, 0, NULL, NULL, NULL, LDAP_NO_LIMIT, &res );
	switch ( rc ) {
	case LDAP_SIZELIMIT_EXCEEDED:
	case LDAP_TIMELIMIT_EXCEEDED:
	case LDAP_SUCCESS:
		if ( ldap_count_entries( ld, res ) == 0 ) {
			if ( rc ) {
				tester_ldap_error( ld, "ldap_search_ext_s", NULL );
			}
			break;
		}

		for ( e = ldap_first_entry( ld, res ); e != NULL; e = ldap_next_entry( ld, e ) )
		{
			struct berval **v = ldap_get_values_len( ld, e, attr );

			if ( v != NULL ) {
				int n = ldap_count_values_len( v );
				int j;

				values = realloc( values, ( nvalues + n + 1 )*sizeof( char * ) );
				for ( j = 0; j < n; j++ ) {
					values[ nvalues + j ] = strdup( v[ j ]->bv_val );
				}
				values[ nvalues + j ] = NULL;
				nvalues += n;
				ldap_value_free_len( v );
			}
		}

		ldap_msgfree( res );

		if ( !values ) {
			fprintf( stderr, "  PID=%ld - Search base=\"%s\" filter=\"%s\" got %d values.\n",
				(long) pid, sbase, filter, nvalues );
			exit(EXIT_FAILURE);
		}

		if ( do_retry == maxretries ) {
			fprintf( stderr, "  PID=%ld - Search base=\"%s\" filter=\"%s\" got %d values.\n",
				(long) pid, sbase, filter, nvalues );
		}

		for ( i = 0; i < innerloop; i++ ) {
			char	buf[ BUFSIZ ];
#if 0	/* use high-order bits for better randomness (Numerical Recipes in "C") */
			int	r = rand() % nvalues;
#endif
			int	r = ((double)nvalues)*rand()/(RAND_MAX + 1.0);

			snprintf( buf, sizeof( buf ), "(%s=%s)", attr, values[ r ] );

			do_search( uri, manager, passwd, sbase, buf, &ld, noattrs, nobind,
					1, maxretries, delay, force, chaserefs );
		}
		break;

	default:
		tester_ldap_error( ld, "ldap_search_ext_s", NULL );
		break;
	}

	fprintf( stderr, " PID=%ld - Search done (%d).\n", (long) pid, rc );

	if ( ld != NULL ) {
		ldap_unbind_ext( ld, NULL, NULL );
	}
}

static void
do_search( char *uri, char *manager, struct berval *passwd,
	char *sbase, char *filter, LDAP **ldp, int noattrs, int nobind,
	int innerloop, int maxretries, int delay, int force, int chaserefs )
{
	LDAP	*ld = ldp ? *ldp : NULL;
	int  	i = 0, do_retry = maxretries;
	char	*attrs[] = { "cn", "sn", NULL };
	int     rc = LDAP_SUCCESS;
	int	version = LDAP_VERSION3;
	char	buf[ BUFSIZ ];


retry:;
	if ( ld == NULL ) {
		ldap_initialize( &ld, uri );
		if ( ld == NULL ) {
			tester_perror( "ldap_initialize", NULL );
			exit( EXIT_FAILURE );
		}

		(void) ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version ); 
		(void) ldap_set_option( ld, LDAP_OPT_REFERRALS,
			chaserefs ? LDAP_OPT_ON : LDAP_OPT_OFF );

		if ( do_retry == maxretries ) {
			fprintf( stderr, "PID=%ld - Search(%d): base=\"%s\", filter=\"%s\".\n",
					(long) pid, innerloop, sbase, filter );
		}

		if ( nobind == 0 ) {
			rc = ldap_sasl_bind_s( ld, manager, LDAP_SASL_SIMPLE, passwd, NULL, NULL, NULL );
			if ( rc != LDAP_SUCCESS ) {
				snprintf( buf, sizeof( buf ),
					"bindDN=\"%s\"", manager );
				tester_ldap_error( ld, "ldap_sasl_bind_s", buf );
				switch ( rc ) {
				case LDAP_BUSY:
				case LDAP_UNAVAILABLE:
					if ( do_retry > 0 ) {
						ldap_unbind_ext( ld, NULL, NULL );
						do_retry--;
						if ( delay != 0 ) {
						    sleep( delay );
						}
						goto retry;
					}
				/* fallthru */
				default:
					break;
				}
				exit( EXIT_FAILURE );
			}
		}
	}

	for ( ; i < innerloop; i++ ) {
		LDAPMessage *res = NULL;

		rc = ldap_search_ext_s( ld, sbase, LDAP_SCOPE_SUBTREE,
				filter, attrs, noattrs, NULL, NULL,
				NULL, LDAP_NO_LIMIT, &res );
		if ( res != NULL ) {
			ldap_msgfree( res );
		}

		if ( rc ) {
			unsigned first = tester_ignore_err( rc );
			/* if ignore.. */
			if ( first ) {
				/* only log if first occurrence */
				if ( force < 2 || first == 1 ) {
					tester_ldap_error( ld, "ldap_search_ext_s", NULL );
				}
				continue;
			}

			/* busy needs special handling */
			snprintf( buf, sizeof( buf ),
				"base=\"%s\" filter=\"%s\"\n",
				sbase, filter );
			tester_ldap_error( ld, "ldap_search_ext_s", buf );
			if ( rc == LDAP_BUSY && do_retry > 0 ) {
				ldap_unbind_ext( ld, NULL, NULL );
				ld = NULL;
				do_retry--;
				goto retry;
			}
			break;
		}
	}

	if ( ldp != NULL ) {
		*ldp = ld;

	} else {
		fprintf( stderr, " PID=%ld - Search done (%d).\n", (long) pid, rc );

		if ( ld != NULL ) {
			ldap_unbind_ext( ld, NULL, NULL );
		}
	}
}
