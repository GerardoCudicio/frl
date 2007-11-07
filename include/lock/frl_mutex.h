#ifndef FRL_MUTEX_H_
#define FRL_MUTEX_H_

#include "lock/frl_scope_guard.h"
#include "lock/frl_mutex_fn.h"
#include "frl_non_copyable.h"

namespace frl
{
	namespace lock
	{		
		class Mutex
			:	private NonCopyable
		{
		private:
			MutexDescriptor mutex;
			volatile Bool isInitialize;
		public:

			typedef frl::lock::ScopeGuard< Mutex > ScopeGuard;
			
			// �����������
			Mutex( void );

			// ����������
			~Mutex( void );

			// ������ ��������
			void Lock( void );

			// ������� ������� ��������
			Bool TryLock( void );

			// ������������ ��������
			void UnLock( void );
		}; // class Mutex
	} // namespace lock
} // FatRat Library

#endif /*FRL_MUTEX_H_*/
