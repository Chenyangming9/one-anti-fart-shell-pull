/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "art_method.h"

#include "arch/context.h"
#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/stringpiece.h"
#include "dex_file-inl.h"
#include "dex_instruction.h"
#include "entrypoints/entrypoint_utils.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "gc/accounting/card_table-inl.h"
#include "interpreter/interpreter.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jni_internal.h"
#include "mapping_table.h"
#include "mirror/abstract_method.h"
#include "mirror/class-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/object-inl.h"
#include "mirror/string.h"
#include "scoped_thread_state_change.h"
#include "well_known_classes.h"
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "runtime.h"
#include <android/log.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#define gettidv1() syscall(__NR_gettid)
#define LOG_TAG "ActivityThread"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace art {

	extern "C" void art_quick_invoke_stub(ArtMethod *, uint32_t *,
					      uint32_t, Thread *, JValue *,
					      const char *);
#if defined(__LP64__) || defined(__arm__) || defined(__i386__)
	extern "C" void art_quick_invoke_static_stub(ArtMethod *,
						     uint32_t *, uint32_t,
						     Thread *, JValue *,
						     const char *);
#endif

	uint8_t *codeitem_end(const uint8_t ** pData) {
		uint32_t num_of_list = DecodeUnsignedLeb128(pData);
		for (; num_of_list > 0; num_of_list--) {
			int32_t num_of_handlers =
			    DecodeSignedLeb128(pData);
			int num = num_of_handlers;
			if (num_of_handlers <= 0) {
				num = -num_of_handlers;
			}
			for (; num > 0; num--) {
				DecodeUnsignedLeb128(pData);
				DecodeUnsignedLeb128(pData);
			}
			if (num_of_handlers <= 0) {
				DecodeUnsignedLeb128(pData);
			}
		}
		return (uint8_t *) (*pData);
	}

	extern "C" char *base64_encode(char *str, long str_len,
				       long *outlen) {
		long len;
		char *res;
		int i, j;
		const char *base64_table =
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		if (str_len % 3 == 0)
			len = str_len / 3 * 4;
		else
			len = (str_len / 3 + 1) * 4;

		res = (char *) malloc(sizeof(char) * (len + 1));
		res[len] = '\0';
		*outlen = len;
		for (i = 0, j = 0; i < len - 2; j += 3, i += 4) {
			res[i] = base64_table[str[j] >> 2];
			res[i + 1] =
			    base64_table[(str[j] & 0x3) << 4 |
					 (str[j + 1] >> 4)];
			res[i + 2] =
			    base64_table[(str[j + 1] & 0xf) << 2 |
					 (str[j + 2] >> 6)];
			res[i + 3] = base64_table[str[j + 2] & 0x3f];
		}

		switch (str_len % 3) {
		case 1:
			res[i - 2] = '=';
			res[i - 1] = '=';
			break;
		case 2:
			res[i - 1] = '=';
			break;
		}

		return res;
	}
	extern "C" void dumpDexFileByExecute(ArtMethod * artmethod)
	 SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
		char *dexfilepath = (char *) malloc(sizeof(char) * 2000);
		if (dexfilepath == nullptr) {
			LOG(INFO) <<
			    "ArtMethod::dumpDexFileByExecute,methodname:"
			    << PrettyMethod(artmethod).
			    c_str() << "malloc 2000 byte failed";
			return;
		}
		int fcmdline = -1;
		char szCmdline[64] = { 0 };
		char szProcName[256] = { 0 };
		int procid = getpid();
		sprintf(szCmdline, "/proc/%d/cmdline", procid);
		fcmdline = open(szCmdline, O_RDONLY, 0644);
		if (fcmdline > 0) {
			read(fcmdline, szProcName, 256);
			close(fcmdline);
		}

		if (szProcName[0]) {

			const DexFile *dex_file = artmethod->GetDexFile();
			const uint8_t *begin_ = dex_file->Begin();	// Start of data.
			size_t size_ = dex_file->Size();	// Length of data.

			memset(dexfilepath, 0, 2000);
			int size_int_ = (int) size_;

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath, "%s", "/sdcard/fart");
			mkdir(dexfilepath, 0777);

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath, "/sdcard/fart/%s",
				szProcName);
			mkdir(dexfilepath, 0777);

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath,
				"/sdcard/fart/%s/%d_dexfile_execute.dex",
				szProcName, size_int_);
			int dexfilefp = open(dexfilepath, O_RDONLY, 0666);
			if (dexfilefp > 0) {
				close(dexfilefp);
				dexfilefp = 0;

			} else {
				dexfilefp =
				    open(dexfilepath, O_CREAT | O_RDWR,
					 0666);
				if (dexfilefp > 0) {
					write(dexfilefp, (void *) begin_,
					      size_);
					fsync(dexfilefp);
					close(dexfilefp);
				}


			}


		}

		if (dexfilepath != nullptr) {
			free(dexfilepath);
			dexfilepath = nullptr;
		}

	}
	extern "C" void dumpArtMethod(ArtMethod * artmethod)
	 SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
		char *dexfilepath = (char *) malloc(sizeof(char) * 2000);
		if (dexfilepath == nullptr) {
			LOG(INFO) <<
			    "ArtMethod::dumpArtMethodinvoked,methodname:"
			    << PrettyMethod(artmethod).
			    c_str() << "malloc 2000 byte failed";
			return;
		}
		int fcmdline = -1;
		char szCmdline[64] = { 0 };
		char szProcName[256] = { 0 };
		int procid = getpid();
		sprintf(szCmdline, "/proc/%d/cmdline", procid);
		fcmdline = open(szCmdline, O_RDONLY, 0644);
		if (fcmdline > 0) {
			read(fcmdline, szProcName, 256);
			close(fcmdline);
		}

		if (szProcName[0]) {

			const DexFile *dex_file = artmethod->GetDexFile();
			const char *methodname =
			    PrettyMethod(artmethod).c_str();
			const uint8_t *begin_ = dex_file->Begin();
			size_t size_ = dex_file->Size();

			memset(dexfilepath, 0, 2000);
			int size_int_ = (int) size_;

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath, "%s", "/sdcard/fart");
			mkdir(dexfilepath, 0777);

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath, "/sdcard/fart/%s",
				szProcName);
			mkdir(dexfilepath, 0777);

			memset(dexfilepath, 0, 2000);
			sprintf(dexfilepath,
				"/sdcard/fart/%s/%d_dexfile.dex",
				szProcName, size_int_);
			int dexfilefp = open(dexfilepath, O_RDONLY, 0666);
			if (dexfilefp > 0) {
				close(dexfilefp);
				dexfilefp = 0;

			} else {
				dexfilefp =
				    open(dexfilepath, O_CREAT | O_RDWR,
					 0666);
				if (dexfilefp > 0) {
					write(dexfilefp, (void *) begin_,
					      size_);
					fsync(dexfilefp);
					close(dexfilefp);
				}


			}
			const DexFile::CodeItem * code_item =
			    artmethod->GetCodeItem();
			if (LIKELY(code_item != nullptr)) {
				int code_item_len = 0;
				uint8_t *item = (uint8_t *) code_item;
				if (code_item->tries_size_ > 0) {
					const uint8_t *handler_data =
					    (const uint8_t *) (DexFile::
							       GetTryItems
							       (*code_item,
								code_item->
								tries_size_));
					uint8_t *tail =
					    codeitem_end(&handler_data);
					code_item_len =
					    (int) (tail - item);
				} else {
					code_item_len =
					    16 +
					    code_item->
					    insns_size_in_code_units_ * 2;
				}
				memset(dexfilepath, 0, 2000);
				int size_int = (int) dex_file->Size();	// Length of data
				uint32_t method_idx =
				    artmethod->get_method_idx();
				sprintf(dexfilepath,
					"/sdcard/fart/%s/%d_%ld.bin",
					szProcName, size_int, gettidv1());
				int fp2 =
				    open(dexfilepath,
					 O_CREAT | O_APPEND | O_RDWR,
					 0666);
				if (fp2 > 0) {
					lseek(fp2, 0, SEEK_END);
					memset(dexfilepath, 0, 2000);
					int offset = (int) (item - begin_);
					sprintf(dexfilepath,
						"{name:%s,method_idx:%d,offset:%d,code_item_len:%d,ins:",
						methodname, method_idx,
						offset, code_item_len);
					int contentlength = 0;
					while (dexfilepath[contentlength]
					       != 0)
						contentlength++;
					write(fp2, (void *) dexfilepath,
					      contentlength);
					long outlen = 0;
					char *base64result =
					    base64_encode((char *) item,
							  (long)
							  code_item_len,
							  &outlen);
					write(fp2, base64result, outlen);
					write(fp2, "};", 2);
					fsync(fp2);
					close(fp2);
					if (base64result != nullptr) {
						free(base64result);
						base64result = nullptr;
					}
				}

			}


		}

		if (dexfilepath != nullptr) {
			free(dexfilepath);
			dexfilepath = nullptr;
		}

	}
	extern "C" void myfartInvoke(ArtMethod * artmethod)
	 SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
		JValue *result = nullptr;
		Thread *self = nullptr;
		uint32_t temp = 6;
		uint32_t *args = &temp;
		uint32_t args_size = 6;
		artmethod->Invoke(self, args, args_size, result, "fart");
	}

	ArtMethod *ArtMethod::
	    FromReflectedMethod(const ScopedObjectAccessAlreadyRunnable &
				soa, jobject jlr_method) {
		auto *abstract_method =
		    soa.Decode < mirror::AbstractMethod * >(jlr_method);
		DCHECK(abstract_method != nullptr);
		return abstract_method->GetArtMethod();
	}

	mirror::String * ArtMethod::GetNameAsString(Thread * self) {
		CHECK(!IsProxyMethod());
		StackHandleScope < 1 > hs(self);
		Handle < mirror::DexCache >
		    dex_cache(hs.NewHandle(GetDexCache()));
		auto *dex_file = dex_cache->GetDexFile();
		uint32_t dex_method_idx = GetDexMethodIndex();
		const DexFile::MethodId & method_id =
		    dex_file->GetMethodId(dex_method_idx);
		return Runtime::Current()->GetClassLinker()->
		    ResolveString(*dex_file, method_id.name_idx_,
				  dex_cache);
	}

	InvokeType ArtMethod::GetInvokeType() {
		// TODO: kSuper?
		if (GetDeclaringClass()->IsInterface()) {
			return kInterface;
		} else if (IsStatic()) {
			return kStatic;
		} else if (IsDirect()) {
			return kDirect;
		} else {
			return kVirtual;
		}
	}

	size_t ArtMethod::NumArgRegisters(const StringPiece & shorty) {
		CHECK_LE(1U, shorty.length());
		uint32_t num_registers = 0;
		for (size_t i = 1; i < shorty.length(); ++i) {
			char ch = shorty[i];
			if (ch == 'D' || ch == 'J') {
				num_registers += 2;
			} else {
				num_registers += 1;
			}
		}
		return num_registers;
	}

	static bool HasSameNameAndSignature(ArtMethod * method1,
					    ArtMethod * method2)
	 SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
		ScopedAssertNoThreadSuspension ants(Thread::Current(),
						    "HasSameNameAndSignature");
		const DexFile *dex_file = method1->GetDexFile();
		const DexFile::MethodId & mid =
		    dex_file->GetMethodId(method1->GetDexMethodIndex());
		if (method1->GetDexCache() == method2->GetDexCache()) {
			const DexFile::MethodId & mid2 =
			    dex_file->GetMethodId(method2->
						  GetDexMethodIndex());
			return mid.name_idx_ == mid2.name_idx_
			    && mid.proto_idx_ == mid2.proto_idx_;
		}
		const DexFile *dex_file2 = method2->GetDexFile();
		const DexFile::MethodId & mid2 =
		    dex_file2->GetMethodId(method2->GetDexMethodIndex());
		if (!DexFileStringEquals
		    (dex_file, mid.name_idx_, dex_file2, mid2.name_idx_)) {
			return false;	// Name mismatch.
		}
		return dex_file->GetMethodSignature(mid) ==
		    dex_file2->GetMethodSignature(mid2);
	}

	ArtMethod *ArtMethod::FindOverriddenMethod(size_t pointer_size) {
		if (IsStatic()) {
			return nullptr;
		}
		mirror::Class * declaring_class = GetDeclaringClass();
		mirror::Class * super_class =
		    declaring_class->GetSuperClass();
		uint16_t method_index = GetMethodIndex();
		ArtMethod *result = nullptr;
		// Did this method override a super class method? If so load the result from the super class'
		// vtable
		if (super_class->HasVTable()
		    && method_index < super_class->GetVTableLength()) {
			result =
			    super_class->GetVTableEntry(method_index,
							pointer_size);
		} else {
			// Method didn't override superclass method so search interfaces
			if (IsProxyMethod()) {
				result =
				    GetDexCacheResolvedMethods()->
				    GetElementPtrSize <
				    ArtMethod * >(GetDexMethodIndex(),
						  pointer_size);
				CHECK_EQ(result,
					 Runtime::Current()->
					 GetClassLinker()->
					 FindMethodForProxy
					 (GetDeclaringClass(), this));
			} else {
				mirror::IfTable * iftable =
				    GetDeclaringClass()->GetIfTable();
				for (size_t i = 0;
				     i < iftable->Count()
				     && result == nullptr; i++) {
					mirror::Class * interface =
					    iftable->GetInterface(i);
					for (size_t j = 0;
					     j <
					     interface->
					     NumVirtualMethods(); ++j) {
						ArtMethod *interface_method
						    =
						    interface->
						    GetVirtualMethod(j,
								     pointer_size);
						if (HasSameNameAndSignature
						    (this,
						     interface_method->
						     GetInterfaceMethodIfProxy
						     (sizeof(void *)))) {
							result =
							    interface_method;
							break;
						}
					}
				}
			}
		}
		DCHECK(result == nullptr
		       ||
		       HasSameNameAndSignature(GetInterfaceMethodIfProxy
					       (sizeof(void *)),
					       result->
					       GetInterfaceMethodIfProxy
					       (sizeof(void *))));
		return result;
	}

	uint32_t ArtMethod::
	    FindDexMethodIndexInOtherDexFile(const DexFile & other_dexfile,
					     uint32_t
					     name_and_signature_idx) {
		const DexFile *dexfile = GetDexFile();
		const uint32_t dex_method_idx = GetDexMethodIndex();
		const DexFile::MethodId & mid =
		    dexfile->GetMethodId(dex_method_idx);
		const DexFile::MethodId & name_and_sig_mid =
		    other_dexfile.GetMethodId(name_and_signature_idx);
		DCHECK_STREQ(dexfile->GetMethodName(mid),
			     other_dexfile.
			     GetMethodName(name_and_sig_mid));
		DCHECK_EQ(dexfile->GetMethodSignature(mid),
			  other_dexfile.
			  GetMethodSignature(name_and_sig_mid));
		if (dexfile == &other_dexfile) {
			return dex_method_idx;
		}
		const char *mid_declaring_class_descriptor =
		    dexfile->StringByTypeIdx(mid.class_idx_);
		const DexFile::StringId * other_descriptor =
		    other_dexfile.
		    FindStringId(mid_declaring_class_descriptor);
		if (other_descriptor != nullptr) {
			const DexFile::TypeId * other_type_id =
			    other_dexfile.FindTypeId(other_dexfile.
						     GetIndexForStringId
						     (*other_descriptor));
			if (other_type_id != nullptr) {
				const DexFile::MethodId * other_mid =
				    other_dexfile.
				    FindMethodId(*other_type_id,
						 other_dexfile.
						 GetStringId
						 (name_and_sig_mid.
						  name_idx_),
						 other_dexfile.
						 GetProtoId
						 (name_and_sig_mid.
						  proto_idx_));
				if (other_mid != nullptr) {
					return other_dexfile.
					    GetIndexForMethodId
					    (*other_mid);
				}
			}
		}
		return DexFile::kDexNoIndex;
	}

	uint32_t ArtMethod::ToDexPc(const uintptr_t pc,
				    bool abort_on_failure) {
		const void *entry_point =
		    GetQuickOatEntryPoint(sizeof(void *));
		uint32_t sought_offset =
		    pc - reinterpret_cast < uintptr_t > (entry_point);
		if (IsOptimized(sizeof(void *))) {
			CodeInfo code_info = GetOptimizedCodeInfo();
			StackMap stack_map =
			    code_info.
			    GetStackMapForNativePcOffset(sought_offset);
			if (stack_map.IsValid()) {
				return stack_map.GetDexPc(code_info);
			}
		} else {
			MappingTable table(entry_point != nullptr ?
					   GetMappingTable
					   (EntryPointToCodePointer
					    (entry_point),
					    sizeof(void *)) : nullptr);
			if (table.TotalSize() == 0) {
				// NOTE: Special methods (see Mir2Lir::GenSpecialCase()) have an empty mapping
				// but they have no suspend checks and, consequently, we never call ToDexPc() for them.
				DCHECK(IsNative() || IsCalleeSaveMethod()
				       || IsProxyMethod()) <<
				    PrettyMethod(this);
				return DexFile::kDexNoIndex;	// Special no mapping case
			}
			// Assume the caller wants a pc-to-dex mapping so check here first.
			typedef MappingTable::PcToDexIterator It;
			for (It cur = table.PcToDexBegin(), end =
			     table.PcToDexEnd(); cur != end; ++cur) {
				if (cur.NativePcOffset() == sought_offset) {
					return cur.DexPc();
				}
			}
			// Now check dex-to-pc mappings.
			typedef MappingTable::DexToPcIterator It2;
			for (It2 cur = table.DexToPcBegin(), end =
			     table.DexToPcEnd(); cur != end; ++cur) {
				if (cur.NativePcOffset() == sought_offset) {
					return cur.DexPc();
				}
			}
		}
		if (abort_on_failure) {
			LOG(FATAL) <<
			    "Failed to find Dex offset for PC offset " <<
			    reinterpret_cast < void *>(sought_offset)
			<< "(PC " << reinterpret_cast <
			    void *>(pc) << ", entry_point=" << entry_point
			    << " current entry_point=" <<
			    GetQuickOatEntryPoint(sizeof(void *))
			<< ") in " << PrettyMethod(this);
		}
		return DexFile::kDexNoIndex;
	}

	uintptr_t ArtMethod::ToNativeQuickPc(const uint32_t dex_pc,
					     bool abort_on_failure) {
		const void *entry_point =
		    GetQuickOatEntryPoint(sizeof(void *));
		MappingTable table(entry_point !=
				   nullptr ?
				   GetMappingTable(EntryPointToCodePointer
						   (entry_point),
						   sizeof(void *)) :
				   nullptr);
		if (table.TotalSize() == 0) {
			DCHECK_EQ(dex_pc, 0U);
			return 0;	// Special no mapping/pc == 0 case
		}
		// Assume the caller wants a dex-to-pc mapping so check here first.
		typedef MappingTable::DexToPcIterator It;
		for (It cur = table.DexToPcBegin(), end =
		     table.DexToPcEnd(); cur != end; ++cur) {
			if (cur.DexPc() == dex_pc) {
				return reinterpret_cast < uintptr_t >
				    (entry_point) + cur.NativePcOffset();
			}
		}
		// Now check pc-to-dex mappings.
		typedef MappingTable::PcToDexIterator It2;
		for (It2 cur = table.PcToDexBegin(), end =
		     table.PcToDexEnd(); cur != end; ++cur) {
			if (cur.DexPc() == dex_pc) {
				return reinterpret_cast < uintptr_t >
				    (entry_point) + cur.NativePcOffset();
			}
		}
		if (abort_on_failure) {
			LOG(FATAL) <<
			    "Failed to find native offset for dex pc 0x" <<
			    std::
			    hex << dex_pc << " in " << PrettyMethod(this);
		}
		return UINTPTR_MAX;
	}

	uint32_t ArtMethod::FindCatchBlock(Handle < mirror::Class >
					   exception_type, uint32_t dex_pc,
					   bool * has_no_move_exception) {
		const DexFile::CodeItem * code_item = GetCodeItem();
		// Set aside the exception while we resolve its type.
		Thread *self = Thread::Current();
		StackHandleScope < 1 > hs(self);
		Handle < mirror::Throwable >
		    exception(hs.NewHandle(self->GetException()));
		self->ClearException();
		// Default to handler not found.
		uint32_t found_dex_pc = DexFile::kDexNoIndex;
		// Iterate over the catch handlers associated with dex_pc.
		for (CatchHandlerIterator it(*code_item, dex_pc);
		     it.HasNext(); it.Next()) {
			uint16_t iter_type_idx = it.GetHandlerTypeIndex();
			// Catch all case
			if (iter_type_idx == DexFile::kDexNoIndex16) {
				found_dex_pc = it.GetHandlerAddress();
				break;
			}
			// Does this catch exception type apply?
			mirror::Class * iter_exception_type =
			    GetClassFromTypeIndex(iter_type_idx, true);
			if (UNLIKELY(iter_exception_type == nullptr)) {
				// Now have a NoClassDefFoundError as exception. Ignore in case the exception class was
				// removed by a pro-guard like tool.
				// Note: this is not RI behavior. RI would have failed when loading the class.
				self->ClearException();
				// Delete any long jump context as this routine is called during a stack walk which will
				// release its in use context at the end.
				delete self->GetLongJumpContext();
				LOG(WARNING) <<
				    "Unresolved exception class when finding catch block: "
				    <<
				    DescriptorToDot
				    (GetTypeDescriptorFromTypeIdx
				     (iter_type_idx));
			} else if (iter_exception_type->
				   IsAssignableFrom(exception_type.
						    Get())) {
				found_dex_pc = it.GetHandlerAddress();
				break;
			}
		}
		if (found_dex_pc != DexFile::kDexNoIndex) {
			const Instruction *first_catch_instr =
			    Instruction::At(&code_item->
					    insns_[found_dex_pc]);
			*has_no_move_exception =
			    (first_catch_instr->Opcode() !=
			     Instruction::MOVE_EXCEPTION);
		}
		// Put the exception back.
		if (exception.Get() != nullptr) {
			self->SetException(exception.Get());
		}
		return found_dex_pc;
	}

	void ArtMethod::AssertPcIsWithinQuickCode(uintptr_t pc) {
		if (IsNative() || IsRuntimeMethod() || IsProxyMethod()) {
			return;
		}
		if (pc == reinterpret_cast < uintptr_t >
		    (GetQuickInstrumentationExitPc())) {
			return;
		}
		const void *code = GetEntryPointFromQuickCompiledCode();
		if (code == GetQuickInstrumentationEntryPoint()) {
			return;
		}
		ClassLinker *class_linker =
		    Runtime::Current()->GetClassLinker();
		if (class_linker->IsQuickToInterpreterBridge(code)
		    || class_linker->IsQuickResolutionStub(code)) {
			return;
		}
		// If we are the JIT then we may have just compiled the method after the
		// IsQuickToInterpreterBridge check.
		jit::Jit * const jit = Runtime::Current()->GetJit();
		if (jit != nullptr &&
		    jit->GetCodeCache()->ContainsCodePtr(reinterpret_cast <
							 const void
							 *>(code))) {
			return;
		}
		/*
		 * During a stack walk, a return PC may point past-the-end of the code
		 * in the case that the last instruction is a call that isn't expected to
		 * return.  Thus, we check <= code + GetCodeSize().
		 *
		 * NOTE: For Thumb both pc and code are offset by 1 indicating the Thumb state.
		 */
		CHECK(PcIsWithinQuickCode
		      (reinterpret_cast < uintptr_t > (code), pc))
		    << PrettyMethod(this)
		    << " pc=" << std::hex << pc
		    << " code=" << code
		    << " size=" <<
		    GetCodeSize(EntryPointToCodePointer
				(reinterpret_cast < const void *>(code)));
	}

	bool ArtMethod::IsEntrypointInterpreter() {
		ClassLinker *class_linker =
		    Runtime::Current()->GetClassLinker();
		const void *oat_quick_code =
		    class_linker->GetOatMethodQuickCodeFor(this);
		return oat_quick_code == nullptr
		    || oat_quick_code !=
		    GetEntryPointFromQuickCompiledCode();
	}

	const void *ArtMethod::GetQuickOatEntryPoint(size_t pointer_size) {
		if (IsAbstract() || IsRuntimeMethod() || IsProxyMethod()) {
			return nullptr;
		}
		Runtime *runtime = Runtime::Current();
		ClassLinker *class_linker = runtime->GetClassLinker();
		const void *code =
		    runtime->GetInstrumentation()->GetQuickCodeFor(this,
								   pointer_size);
		// On failure, instead of null we get the quick-generic-jni-trampoline for native method
		// indicating the generic JNI, or the quick-to-interpreter-bridge (but not the trampoline)
		// for non-native methods.
		if (class_linker->IsQuickToInterpreterBridge(code) ||
		    class_linker->IsQuickGenericJniStub(code)) {
			return nullptr;
		}
		return code;
	}

#ifndef NDEBUG
	uintptr_t ArtMethod::NativeQuickPcOffset(const uintptr_t pc,
						 const void
						 *quick_entry_point) {
		CHECK_NE(quick_entry_point, GetQuickToInterpreterBridge());
		CHECK_EQ(quick_entry_point,
			 Runtime::Current()->GetInstrumentation()->
			 GetQuickCodeFor(this, sizeof(void *)));
		return pc - reinterpret_cast < uintptr_t >
		    (quick_entry_point);
	}
#endif

	void ArtMethod::Invoke(Thread * self, uint32_t * args,
			       uint32_t args_size, JValue * result,
			       const char *shorty) {


		if (self == nullptr) {
			dumpArtMethod(this);
			return;
		}

		//add-dc
		//这里只对<clinit>方法的codeitem进行dump，会由Class.forName(clsName)进行触发。其它方法都可以在myfartInvoke中主动调用。
		if (this->PrettyMethod().rfind("<clinit>()") > 0){ 

			char szProcName[256] = {0};
			int procid = getpid();
			char szCmdline[256] = {'\0'};
			sprintf(szCmdline, "/proc/%d/cmdline", procid);
			int fcmdline = open(szCmdline, O_RDONLY, 0644);
			if (fcmdline > 0)
			{
				int result2 = read(fcmdline, szProcName, 256);
				if (result2 < 0)
				{
					ALOGI("%s","ArtMethod::dumpdexfilebyArtMethod,open cmdline file file error");
				}
				close(fcmdline);
			}
			//这个包名需要和ActivityThread.java中给定的脱壳app的包名一致。即整个脱壳过程只对一个apk的codeitem进行dump
			if (strcmp(szProcName, "com.example.test") == 0){
				//ALOGI("szProcName %s", szProcName);
				ALOGI("<clinit> of %s is called", this->PrettyMethod().c_str());
				dumpArtMethod(this);
			}
		}
		//add-dc

		if (UNLIKELY
		    (__builtin_frame_address(0) < self->GetStackEnd())) {
			ThrowStackOverflowError(self);
			return;
		}

		if (kIsDebugBuild) {
			self->AssertThreadSuspensionIsAllowable();
			CHECK_EQ(kRunnable, self->GetState());
			CHECK_STREQ(GetInterfaceMethodIfProxy
				    (sizeof(void *))->GetShorty(), shorty);
		}
		// Push a transition back into managed code onto the linked list in thread.
		ManagedStack fragment;
		self->PushManagedStackFragment(&fragment);

		Runtime *runtime = Runtime::Current();
		// Call the invoke stub, passing everything as arguments.
		// If the runtime is not yet started or it is required by the debugger, then perform the
		// Invocation by the interpreter.
		if (UNLIKELY
		    (!runtime->IsStarted()
		     || Dbg::IsForcedInterpreterNeededForCalling(self,
								 this))) {
			if (IsStatic()) {
				art::interpreter::
				    EnterInterpreterFromInvoke(self, this,
							       nullptr,
							       args,
							       result);
			} else {
				mirror::Object * receiver =
				    reinterpret_cast < StackReference <
				    mirror::Object >
				    *>(&args[0])->AsMirrorPtr();
				art::interpreter::
				    EnterInterpreterFromInvoke(self, this,
							       receiver,
							       args + 1,
							       result);
			}
		} else {
			DCHECK_EQ(runtime->GetClassLinker()->
				  GetImagePointerSize(), sizeof(void *));

			constexpr bool kLogInvocationStartAndReturn =
			    false;
			bool have_quick_code =
			    GetEntryPointFromQuickCompiledCode() !=
			    nullptr;
			if (LIKELY(have_quick_code)) {
				if (kLogInvocationStartAndReturn) {
					LOG(INFO) <<
					    StringPrintf
					    ("Invoking '%s' quick code=%p static=%d",
					     PrettyMethod(this).c_str(),
					     GetEntryPointFromQuickCompiledCode
					     (),
					     static_cast <
					     int >(IsStatic()? 1 : 0));
				}
				// Ensure that we won't be accidentally calling quick compiled code when -Xint.
				if (kIsDebugBuild
				    && runtime->GetInstrumentation()->
				    IsForcedInterpretOnly()) {
					DCHECK(!runtime->UseJit());
					CHECK(IsEntrypointInterpreter())
					    <<
					    "Don't call compiled code when -Xint "
					    << PrettyMethod(this);
				}
#if defined(__LP64__) || defined(__arm__) || defined(__i386__)
				if (!IsStatic()) {
					(*art_quick_invoke_stub) (this,
								  args,
								  args_size,
								  self,
								  result,
								  shorty);
				} else {
					(*art_quick_invoke_static_stub)
					    (this, args, args_size, self,
					     result, shorty);
				}
#else
				(*art_quick_invoke_stub) (this, args,
							  args_size, self,
							  result, shorty);
#endif
				if (UNLIKELY
				    (self->GetException() ==
				     Thread::
				     GetDeoptimizationException())) {
					// Unusual case where we were running generated code and an
					// exception was thrown to force the activations to be removed from the
					// stack. Continue execution in the interpreter.
					self->ClearException();
					ShadowFrame *shadow_frame =
					    self->
					    PopStackedShadowFrame
					    (StackedShadowFrameType::
					     kDeoptimizationShadowFrame);
					result->SetJ(self->
						     PopDeoptimizationReturnValue
						     ().GetJ());
					self->SetTopOfStack(nullptr);
					self->
					    SetTopOfShadowStack
					    (shadow_frame);
					interpreter::
					    EnterInterpreterFromDeoptimize
					    (self, shadow_frame, result);
				}
				if (kLogInvocationStartAndReturn) {
					LOG(INFO) <<
					    StringPrintf
					    ("Returned '%s' quick code=%p",
					     PrettyMethod(this).c_str(),
					     GetEntryPointFromQuickCompiledCode
					     ());
				}
			} else {
				LOG(INFO) << "Not invoking '" <<
				    PrettyMethod(this) << "' code=null";
				if (result != nullptr) {
					result->SetJ(0);
				}
			}
		}

		// Pop transition.
		self->PopManagedStackFragment(fragment);
	}

// Counts the number of references in the parameter list of the corresponding method.
// Note: Thus does _not_ include "this" for non-static methods.
	static uint32_t GetNumberOfReferenceArgsWithoutReceiver(ArtMethod *
								method)
	 SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
		uint32_t shorty_len;
		const char *shorty = method->GetShorty(&shorty_len);
		uint32_t refs = 0;
		for (uint32_t i = 1; i < shorty_len; ++i) {
			if (shorty[i] == 'L') {
				refs++;
			}
		}
		return refs;
	}

	QuickMethodFrameInfo ArtMethod::GetQuickFrameInfo() {
		Runtime *runtime = Runtime::Current();

		if (UNLIKELY(IsAbstract())) {
			return runtime->
			    GetCalleeSaveMethodFrameInfo(Runtime::
							 kRefsAndArgs);
		}
		// This goes before IsProxyMethod since runtime methods have a null declaring class.
		if (UNLIKELY(IsRuntimeMethod())) {
			return runtime->GetRuntimeMethodFrameInfo(this);
		}
		// For Proxy method we add special handling for the direct method case  (there is only one
		// direct method - constructor). Direct method is cloned from original
		// java.lang.reflect.Proxy class together with code and as a result it is executed as usual
		// quick compiled method without any stubs. So the frame info should be returned as it is a
		// quick method not a stub. However, if instrumentation stubs are installed, the
		// instrumentation->GetQuickCodeFor() returns the artQuickProxyInvokeHandler instead of an
		// oat code pointer, thus we have to add a special case here.
		if (UNLIKELY(IsProxyMethod())) {
			if (IsDirect()) {
				CHECK(IsConstructor());
				return
				    GetQuickFrameInfo
				    (EntryPointToCodePointer
				     (GetEntryPointFromQuickCompiledCode
				      ()));
			} else {
				return runtime->
				    GetCalleeSaveMethodFrameInfo(Runtime::
								 kRefsAndArgs);
			}
		}

		const void *entry_point =
		    runtime->GetInstrumentation()->GetQuickCodeFor(this,
								   sizeof
								   (void
								    *));
		ClassLinker *class_linker = runtime->GetClassLinker();
		// On failure, instead of null we get the quick-generic-jni-trampoline for native method
		// indicating the generic JNI, or the quick-to-interpreter-bridge (but not the trampoline)
		// for non-native methods. And we really shouldn't see a failure for non-native methods here.
		DCHECK(!class_linker->
		       IsQuickToInterpreterBridge(entry_point));

		if (class_linker->IsQuickGenericJniStub(entry_point)) {
			// Generic JNI frame.
			DCHECK(IsNative());
			uint32_t handle_refs =
			    GetNumberOfReferenceArgsWithoutReceiver(this) +
			    1;
			size_t scope_size =
			    HandleScope::SizeOf(handle_refs);
			QuickMethodFrameInfo callee_info =
			    runtime->
			    GetCalleeSaveMethodFrameInfo(Runtime::
							 kRefsAndArgs);

			// Callee saves + handle scope + method ref + alignment
			// Note: -sizeof(void*) since callee-save frame stores a whole method pointer.
			size_t frame_size =
			    RoundUp(callee_info.FrameSizeInBytes() -
				    sizeof(void *) + sizeof(ArtMethod *) +
				    scope_size, kStackAlignment);
			return QuickMethodFrameInfo(frame_size,
						    callee_info.
						    CoreSpillMask(),
						    callee_info.
						    FpSpillMask());
		}

		const void *code_pointer =
		    EntryPointToCodePointer(entry_point);
		return GetQuickFrameInfo(code_pointer);
	}

	void ArtMethod::RegisterNative(const void *native_method,
				       bool is_fast) {
		CHECK(IsNative()) << PrettyMethod(this);
		CHECK(!IsFastNative()) << PrettyMethod(this);
		CHECK(native_method != nullptr) << PrettyMethod(this);
		if (is_fast) {
			SetAccessFlags(GetAccessFlags() | kAccFastNative);
		}
		SetEntryPointFromJni(native_method);
	}

	void ArtMethod::UnregisterNative() {
		CHECK(IsNative() && !IsFastNative()) << PrettyMethod(this);
		// restore stub to lookup native pointer via dlsym
		RegisterNative(GetJniDlsymLookupStub(), false);
	}

	bool ArtMethod::EqualParameters(Handle < mirror::ObjectArray <
					mirror::Class >> params) {
		auto *dex_cache = GetDexCache();
		auto *dex_file = dex_cache->GetDexFile();
		const auto & method_id =
		    dex_file->GetMethodId(GetDexMethodIndex());
		const auto & proto_id =
		    dex_file->GetMethodPrototype(method_id);
		const DexFile::TypeList * proto_params =
		    dex_file->GetProtoParameters(proto_id);
		auto count =
		    proto_params != nullptr ? proto_params->Size() : 0u;
		auto param_len =
		    params.Get() != nullptr ? params->GetLength() : 0u;
		if (param_len != count) {
			return false;
		}
		auto *cl = Runtime::Current()->GetClassLinker();
		for (size_t i = 0; i < count; ++i) {
			auto type_idx =
			    proto_params->GetTypeItem(i).type_idx_;
			auto *type = cl->ResolveType(type_idx, this);
			if (type == nullptr) {
				Thread::Current()->
				    AssertPendingException();
				return false;
			}
			if (type != params->GetWithoutChecks(i)) {
				return false;
			}
		}
		return true;
	}

}				// namespace art
