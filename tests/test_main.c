/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <stdlib.h>

extern Suite *rng_suite(void);
extern Suite *crc32_suite(void);
extern Suite *xor_suite(void);
extern Suite *rs_suite(void);
extern Suite *chunk_suite(void);
extern Suite *meta_suite(void);
extern Suite *control_suite(void);
extern Suite *codec_suite(void);
extern Suite *actions_suite(void);

int main(void)
{
	SRunner *sr = srunner_create(rng_suite());

	srunner_add_suite(sr, crc32_suite());
	srunner_add_suite(sr, xor_suite());
	srunner_add_suite(sr, rs_suite());
	srunner_add_suite(sr, chunk_suite());
	srunner_add_suite(sr, meta_suite());
	srunner_add_suite(sr, control_suite());
	srunner_add_suite(sr, codec_suite());
	srunner_add_suite(sr, actions_suite());

	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);

	int nfailed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nfailed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
