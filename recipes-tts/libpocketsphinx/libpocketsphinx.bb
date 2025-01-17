DESCRIPTION = "CMU Pocketsphinx"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/\
${LICENSE};md5=89aea4e17d99a7cacdbeed46a0096b10"

MY_PN = "libpocketsphinx-dev"
PROVIDES ="libpocketsphinx"
PROVIDES ="libpocketsphinx-dev"

# Unless we specifically state this, binaries using the library will fail QA
RPROVIDES:${PN} ="libpocketsphinx.so"
DEPENDS = "popt"
PR = "r0"
S = "${WORKDIR}"
INSANE_SKIP:${PN} += "dev-so"
FILES:${PN} += "/usr/lib/*.so*"

INSANE_SKIP_${PN} += " ldflags"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
SOLIBS = ".so*"
FILES_SOLIBSDEV = ""

SRC_URI="file://include/pocketsphinx.h \
        file://include/pocketsphinx/model.h \
        file://include/pocketsphinx/logmath.h \
        file://include/pocketsphinx/export.h \
        file://include/pocketsphinx/lattice.h \
        file://include/pocketsphinx/alignment.h \
        file://include/pocketsphinx/mllr.h \
        file://include/pocketsphinx/prim_type.h \
        file://include/pocketsphinx/vad.h \
        file://include/pocketsphinx/err.h \
        file://include/pocketsphinx/endpointer.h \
        file://include/pocketsphinx/search.h \
        file://include/pocketsphinx/sphinx_config.h \
        file://model/en-us/en-us.lm.bin \
        file://model/en-us/en-us \
        file://model/en-us/en-us/feat.params \
        file://model/en-us/en-us/README \
        file://model/en-us/en-us/noisedict \
        file://model/en-us/en-us/sendump \
        file://model/en-us/en-us/variances \
        file://model/en-us/en-us/mdef \
        file://model/en-us/en-us/transition_matrices \
        file://model/en-us/en-us/means \
        file://model/en-us/cmudict-en-us.dict \
        file://model/en-us/en-us-phone.lm.bin \
        file://src/ngram_search_fwdtree.c \
        file://src/fast_ptm.txt \
        file://src/feat/cmn.h \
        file://src/feat/feat.c \
        file://src/feat/cmn_live.c \
        file://src/feat/agc.h \
        file://src/feat/feat.h \
        file://src/feat/lda.c \
        file://src/feat/cmn.c \
        file://src/feat/agc.c \
        file://src/ps_lattice.c \
        file://src/s2_semi_mgau.h \
        file://src/fsg_search.c \
        file://src/util/profile.h \
        file://src/util/f2c_lite.c \
        file://src/util/blkarray_list.h \
        file://src/util/strfuncs.c \
        file://src/util/bitvec.h \
        file://src/util/genrand.h \
        file://src/util/filename.c \
        file://src/util/blkarray_list.c \
        file://src/util/matrix.h \
        file://src/util/f2c.h \
        file://src/util/cmd_ln.h \
        file://src/util/listelem_alloc.c \
        file://src/util/bio.c \
        file://src/util/priority_queue.c \
        file://src/util/glist.h \
        file://src/util/pio.c \
        file://src/util/priority_queue.h \
        file://src/util/pio.h \
        file://src/util/matrix.c \
        file://src/util/soundfiles.c \
        file://src/util/profile.c \
        file://src/util/clapack_lite.h \
        file://src/util/glist.c \
        file://src/util/slamch.c \
        file://src/util/vector.h \
        file://src/util/wrapped_routines \
        file://src/util/heap.c \
        file://src/util/mmio.c \
        file://src/util/vector.c \
        file://src/util/byteorder.h \
        file://src/util/ckd_alloc.c \
        file://src/util/filename.h \
        file://src/util/blas_lite.c \
        file://src/util/genrand.c \
        file://src/util/hash_table.c \
        file://src/util/listelem_alloc.h \
        file://src/util/slapack_lite.c \
        file://src/util/ckd_alloc.h \
        file://src/util/bio.h \
        file://src/util/case.c \
        file://src/util/err.c \
        file://src/util/errno.c \
        file://src/util/cmd_ln.c \
        file://src/util/mmio.h \
        file://src/util/heap.h \
        file://src/util/case.h \
        file://src/util/strfuncs.h \
        file://src/util/logmath.c \
        file://src/util/hash_table.h \
        file://src/util/bitvec.c \
        file://src/util/dtoa.c \
        file://src/ptm_mgau.c \
        file://src/ngram_search_fwdtree.h \
        file://src/acmod.c \
        file://src/ps_alignment_internal.h \
        file://src/s2_semi_mgau.c \
        file://src/phone_loop_search.c \
        file://src/mdef.h \
        file://src/ngram_search_fwdflat.c \
        file://src/ms_gauden.h \
        file://src/pocketsphinx.c \
        file://src/ptm_mgau.h \
        file://src/kws_search.h \
        file://src/ngram_search.h \
        file://src/dict2pid.c \
        file://src/ps_alignment.c \
        file://src/lm/ngram_model_internal.h \
        file://src/lm/ngram_model.h \
        file://src/lm/jsgf_scanner.c \
        file://src/lm/bitarr.c \
        file://src/lm/ngram_model_trie.c \
        file://src/lm/ngram_model.c \
        file://src/lm/jsgf_parser.h \
        file://src/lm/fsg_model.c \
        file://src/lm/bitarr.h \
        file://src/lm/jsgf_parser.y \
        file://src/lm/jsgf.h \
        file://src/lm/ngrams_raw.h \
        file://src/lm/jsgf_scanner.h \
        file://src/lm/jsgf.c \
        file://src/lm/lm_trie_quant.c \
        file://src/lm/ngram_model_trie.h \
        file://src/lm/jsgf_internal.h \
        file://src/lm/lm_trie.h \
        file://src/lm/lm_trie_quant.h \
        file://src/lm/ngram_model_set.h \
        file://src/lm/fsg_model.h \
        file://src/lm/lm_trie.c \
        file://src/lm/jsgf_parser.c \
        file://src/lm/ngram_model_set.c \
        file://src/lm/_jsgf_scanner.l \
        file://src/lm/ngrams_raw.c \
        file://src/ngram_search_fwdflat.h \
        file://src/jsmn.h \
        file://src/bin_mdef.h \
        file://src/fsg_history.h \
        file://src/state_align_search.h \
        file://src/ps_config.c \
        file://src/phone_loop_search.h \
        file://src/ms_mgau.h \
        file://src/tmat.h \
        file://src/kws_search.c \
        file://src/state_align_search.c \
        file://src/ms_senone.h \
        file://src/ms_senone.c \
        file://src/ps_lattice_internal.h \
        file://src/config_macro.h \
        file://src/dict2pid.h \
        file://src/dict.c \
        file://src/acmod.h \
        file://src/hmm.c \
        file://src/bin_mdef.c \
        file://src/kws_detections.c \
        file://src/tmat.c \
        file://src/fe/yin.h \
        file://src/fe/fe_warp.h \
        file://src/fe/fe_warp_inverse_linear.c \
        file://src/fe/fe_warp_piecewise_linear.c \
        file://src/fe/fixpoint.h \
        file://src/fe/fe_warp_affine.c \
        file://src/fe/fe_warp_inverse_linear.h \
        file://src/fe/fe_internal.h \
        file://src/fe/fe_noise.h \
        file://src/fe/fe.h \
        file://src/fe/fe_interface.c \
        file://src/fe/fe_warp_affine.h \
        file://src/fe/fe_noise.c \
        file://src/fe/fe_warp_piecewise_linear.h \
        file://src/fe/fe_sigproc.c \
        file://src/fe/fe_type.h \
        file://src/fe/fixlog.c \
        file://src/fe/yin.c \
        file://src/fe/fe_warp.c \
        file://src/fsg_lextree.h \
        file://src/s3types.h \
        file://src/allphone_search.h \
        file://src/ms_mgau.c \
        file://src/hmm.h \
        file://src/mdef.c \
        file://src/rtc_base/typedefs.h \
        file://src/rtc_base/compile_assert_c.h \
        file://src/rtc_base/checks.h \
        file://src/rtc_base/sanitizer.h \
        file://src/rtc_base/system \
        file://src/rtc_base/system/arch.h \
        file://src/ngram_search.c \
        file://src/dict.h \
        file://src/tied_mgau_common.h \
        file://src/ps_mllr.c \
        file://src/fsg_history.c \
        file://src/common_audio/vad/vad_gmm.h \
        file://src/common_audio/vad/vad_sp.c \
        file://src/common_audio/vad/include \
        file://src/common_audio/vad/include/webrtc_vad.h \
        file://src/common_audio/vad/vad_filterbank.h \
        file://src/common_audio/vad/vad_core.h \
        file://src/common_audio/vad/vad_gmm.c \
        file://src/common_audio/vad/webrtc_vad.c \
        file://src/common_audio/vad/vad_sp.h \
        file://src/common_audio/vad/vad_filterbank.c \
        file://src/common_audio/vad/vad_core.c \
        file://src/common_audio/signal_processing \
        file://src/common_audio/signal_processing/resample_by_2_internal.c \
        file://src/common_audio/signal_processing/get_scaling_square.c \
        file://src/common_audio/signal_processing/resample_fractional.c \
        file://src/common_audio/signal_processing/spl_inl.c \
        file://src/common_audio/signal_processing/downsample_fast.c \
        file://src/common_audio/signal_processing/include \
        file://src/common_audio/signal_processing/include/spl_inl.h \
        file://src/common_audio/signal_processing/include/signal_processing_library.h \
        file://src/common_audio/signal_processing/resample.c \
        file://src/common_audio/signal_processing/min_max_operations.c \
        file://src/common_audio/signal_processing/energy.c \
        file://src/common_audio/signal_processing/resample_48khz.c \
        file://src/common_audio/signal_processing/cross_correlation.c \
        file://src/common_audio/signal_processing/vector_scaling_operations.c \
        file://src/common_audio/signal_processing/division_operations.c \
        file://src/common_audio/signal_processing/resample_by_2_internal.h \
        file://src/ps_vad.c \
        file://src/ps_endpointer.c \
        file://src/fsg_search_internal.h \
        file://src/kws_detections.h \
        file://src/pocketsphinx_internal.h \
        file://src/allphone_search.c \
        file://src/fsg_lextree.c \
        file://src/ms_gauden.c"


FILES:${PN} += "/usr/pocketsphinx/models/en-us/*"
FILES:${PN} += "/usr/pocketsphinx/models/en-us/en-us/*"

SRCFILES="src/acmod.c src/allphone_search.c src/bin_mdef.c src/common_audio/vad/vad_gmm.c src/common_audio/vad/webrtc_vad.c src/common_audio/vad/vad_filterbank.c src/common_audio/vad/vad_core.c src/common_audio/vad/vad_sp.c src/common_audio/signal_processing/division_operations.c src/common_audio/signal_processing/resample_48khz.c src/common_audio/signal_processing/resample.c src/common_audio/signal_processing/resample_fractional.c src/common_audio/signal_processing/downsample_fast.c src/common_audio/signal_processing/min_max_operations.c src/common_audio/signal_processing/cross_correlation.c src/common_audio/signal_processing/vector_scaling_operations.c src/common_audio/signal_processing/resample_by_2_internal.c src/common_audio/signal_processing/energy.c src/common_audio/signal_processing/spl_inl.c src/common_audio/signal_processing/get_scaling_square.c src/dict2pid.c src/dict.c src/fe/fe_sigproc.c src/fe/fixlog.c src/fe/fe_warp_inverse_linear.c src/fe/fe_noise.c src/fe/fe_warp.c src/fe/fe_interface.c src/fe/fe_warp_affine.c src/fe/yin.c src/fe/fe_warp_piecewise_linear.c src/feat/cmn.c src/feat/agc.c src/feat/cmn_live.c src/feat/feat.c src/feat/lda.c src/fsg_history.c src/fsg_lextree.c src/fsg_search.c src/hmm.c src/kws_detections.c src/kws_search.c src/lm/lm_trie_quant.c src/lm/ngram_model_trie.c src/lm/fsg_model.c src/lm/jsgf.c src/lm/ngram_model_set.c src/lm/ngrams_raw.c src/lm/jsgf_scanner.c src/lm/bitarr.c src/lm/ngram_model.c src/lm/lm_trie.c src/lm/jsgf_parser.c src/mdef.c src/ms_gauden.c src/ms_mgau.c src/ms_senone.c src/ngram_search.c src/ngram_search_fwdflat.c src/ngram_search_fwdtree.c src/phone_loop_search.c src/pocketsphinx.c src/ps_alignment.c src/ps_config.c src/ps_endpointer.c src/ps_lattice.c src/ps_mllr.c src/ps_vad.c src/ptm_mgau.c src/s2_semi_mgau.c src/state_align_search.c src/tmat.c src/util/strfuncs.c src/util/dtoa.c src/util/case.c src/util/filename.c src/util/slamch.c src/util/cmd_ln.c src/util/blas_lite.c src/util/blkarray_list.c src/util/vector.c src/util/mmio.c src/util/hash_table.c src/util/err.c src/util/ckd_alloc.c src/util/slapack_lite.c src/util/matrix.c src/util/bio.c src/util/heap.c src/util/priority_queue.c src/util/bitvec.c src/util/profile.c src/util/errno.c src/util/logmath.c src/util/glist.c src/util/f2c_lite.c src/util/listelem_alloc.c src/util/pio.c src/util/genrand.c src/util/soundfiles.c "

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} -iquote ${WORKDIR}/src/ -I${WORKDIR}/include/ -I${WORKDIR}/src/ ${SRCFILES} -shared -fpic -O2  -o libpocketsphinx.so.0 -lm
}

do_install() {
    install -d ${D}${libdir}
    install -d ${D}/usr/pocketsphinx/models/en-us/en-us
    install -d ${D}${includedir}
    install -d ${D}${includedir}/pocketsphinx

    install -m 0755  ${S}/libpocketsphinx.so.0 ${D}${libdir}/
    install -m 0644 ${WORKDIR}/include/pocketsphinx.h ${D}${includedir}/ 
    install -m 0644 ${WORKDIR}/include/pocketsphinx/* ${D}${includedir}/pocketsphinx/
    install -m 0644 ${WORKDIR}/model/en-us/en-us/*  ${D}/usr/pocketsphinx/models/en-us/en-us/
    install -m 0644 ${WORKDIR}/model/en-us/cmudict-en-us.dict  ${D}/usr/pocketsphinx/models/en-us/
    install -m 0644 ${WORKDIR}/model/en-us/en-us-phone.lm.bin  ${D}/usr/pocketsphinx/models/en-us/
    install -m 0644 ${WORKDIR}/model/en-us/en-us.lm.bin  ${D}/usr/pocketsphinx/models/en-us/

    # We need the symlink so other recipes can find the library
    ln -sf -r ${D}/usr/lib/libpocketsphinx.so.0 ${D}/usr/lib//libpocketsphinx.so
}
