#include "frl_platform.h"
#if( FRL_PLATFORM == FRL_PLATFORM_WIN32 )
#include <iostream>
#include "opc/frl_opc_group.h"
#include "opc/frl_opc_util.h"

namespace frl
{
	namespace opc
	{
		HRESULT STDMETHODCALLTYPE Group::QueryInterface( /* [in] */ REFIID iid, /* [iid_is][out] */ void** ppInterface )
		{
			if( ppInterface == NULL )
				return E_POINTER;

			*ppInterface = NULL;

			if( iid == __uuidof( IOPCGroupStateMgt ) || iid == IID_IUnknown )
			{
				*ppInterface = (dynamic_cast<IOPCGroupStateMgt*>(this));
				AddRef();
				return S_OK;
			}

			if( iid == __uuidof( IOPCItemMgt ) )
			{
				*ppInterface = (dynamic_cast<IOPCItemMgt*>(this));
				AddRef();
				return S_OK;
			}

			if( iid == __uuidof( IOPCSyncIO ) )
			{
				*ppInterface = (dynamic_cast<IOPCSyncIO*>(this));
				AddRef();
				return S_OK;
			}

			if( iid == __uuidof( IOPCAsyncIO2 ) )
			{
				*ppInterface = (dynamic_cast<IOPCAsyncIO2*>(this));
				AddRef();
				return S_OK;
			}

			if( iid == __uuidof( IConnectionPointContainer ) )
			{
				*ppInterface = (dynamic_cast< IConnectionPointContainer* >(this));
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE Group::AddRef( void )
		{
			return ::InterlockedIncrement( &refCount );
		}

		ULONG STDMETHODCALLTYPE Group::Release( void )
		{
			LONG tmp = ::InterlockedDecrement( &refCount );
			if( tmp == 0 )
				delete this;
			return tmp;
		}

		HRESULT STDMETHODCALLTYPE Group::CreateInstance( IUnknown** ippUnknown, const CLSID* pClsid )
		{
			if( ippUnknown == NULL) return E_POINTER;
				*ippUnknown = NULL;
				
				Group* pObject = new Group();
				pObject->clsid = pClsid;
				HRESULT hResult = pObject->QueryInterface(IID_IUnknown, (void**)ippUnknown);
				pObject->Release();
				return hResult;
		}

		REFCLSID Group::GetCLSID()
		{
			return *clsid;
		}

		Group::Group()
			:	refCount( 0 ),
				timerRead( this, &Group::onReadTimer ),
				timerWrite( this, &Group::onWriteTimer ),
				timerRefresh( this, &Group::onRefreshTimer ),
				timerUpdate( this, &Group::onUpdateTimer )
		{
			name = util::getUniqueName();
			Init();
		}

		Group::Group( const String &groupName )
			:	name( groupName ),
				refCount( 0 ),
				timerRead( this, &Group::onReadTimer ),
				timerWrite( this, &Group::onWriteTimer ),
				timerRefresh( this, &Group::onRefreshTimer ),
				timerUpdate( this, &Group::onUpdateTimer )
		{
			Init();
		}

		Group::~Group()
		{
			for( std::list< AsyncRequest* >::iterator it = asyncReadList.begin(); it != asyncReadList.end(); ++it )
				delete (*it);

			for( std::list< AsyncRequest* >::iterator it = asyncRefreshList.begin(); it != asyncRefreshList.end(); ++it )
				delete (*it);

			for( std::list< AsyncRequest* >::iterator it = asyncWriteList.begin(); it != asyncWriteList.end(); ++it )
				delete (*it);

			if( ! itemList.empty())
			{
				std::map<OPCHANDLE, GroupItem*>::iterator it;
				for( it = itemList.begin(); it != itemList.end(); ++it )
				{
					GroupItem *item = (*it).second;
					delete item;
				}
			}
		}

		void Group::Init()
		{
			serverHandle = util::getUniqueServerHandle();
			clientHandle = NULL;

			actived = False;
			enabled = True;
			deleted = False;
			updateRate = 500;
			timeBias = 0;
			deadband = 0;
			localeID = LOCALE_NEUTRAL;
			keepAlive = 0;
			lastUpdate = util::getFileTime();
			tickOffset = -1;
			registerInterface(IID_IOPCDataCallback);

			timerRead.setTimer( 1 );
			timerWrite.setTimer( 1 );
			timerRefresh.setTimer( 1 );
			timerUpdate.setTimer( updateRate );

			timerRead.start();
			timerWrite.start();
			timerRefresh.start();
		}

		void Group::setServerPtr( OPCServer *serverPtr )
		{
			server = serverPtr;
		}

		OPCHANDLE Group::getServerHandle()
		{
			return serverHandle;
		}

		const String Group::getName()
		{
			return name;
		}

		LONG Group::getRefCount()
		{
			return refCount;
		}

		frl::Bool Group::isDeleted()
		{
			return deleted;
		}

		void Group::isDeleted( Bool deleteFlag )
		{
			deleted = deleteFlag;
		}

		void Group::setName( const String &newName )
		{
			name = newName;
		}

		void Group::onReadTimer()
		{
			if( ! asyncReadList.size() )
				return;

			IOPCDataCallback* ipCallback = NULL;
			HRESULT hResult = getCallback( IID_IOPCDataCallback, (IUnknown**)&ipCallback );

			if( FAILED( hResult ) )
				return;

			lock::Mutex::ScopeGuard guard( groupGuard );
			for( std::list< AsyncRequest* >::iterator it = asyncReadList.begin(), remIt; it != asyncReadList.end(); it = remIt )
			{
				remIt = it;
				++remIt;
				if( (*it)->isCancelled() )
				{
					ipCallback->OnCancelComplete( (*it)->getTransactionID(), clientHandle );
				}
				else
				{
					if( (*it)->getCounts() != 0 )
						doAsyncRead( &ipCallback, (*it) );
				}
				delete (*it);
				asyncReadList.erase( it );
			}
			ipCallback->Release();
		}

		void Group::onRefreshTimer()
		{
			if( ! asyncRefreshList.size() )
				return;

			IOPCDataCallback* ipCallback = NULL;
			HRESULT hResult = getCallback( IID_IOPCDataCallback, (IUnknown**)&ipCallback );

			if( FAILED( hResult ) )
				return;

			lock::Mutex::ScopeGuard guard( groupGuard );
			for( std::list< AsyncRequest* >::iterator it = asyncRefreshList.begin(), remIt; it != asyncRefreshList.end(); it = remIt )
			{
				remIt = it;
				++remIt;
				if( (*it)->isCancelled() )
				{
					ipCallback->OnCancelComplete( (*it)->getTransactionID(), clientHandle );
				}
				else
				{
					if( (*it)->getCounts() != 0 )
						doAsyncRefresh( &ipCallback, (*it) );
				}				
				delete (*it);
				asyncRefreshList.erase( it );
			}
			ipCallback->Release();
		}

		void Group::onWriteTimer()
		{
			if( ! asyncWriteList.size() )
				return;

			IOPCDataCallback* ipCallback = NULL;
			HRESULT hResult = getCallback( IID_IOPCDataCallback, (IUnknown**)&ipCallback );

			if( FAILED( hResult ) )
				return;

			lock::Mutex::ScopeGuard guard( groupGuard );
			for( std::list< AsyncRequest* >::iterator it = asyncWriteList.begin(), remIt; it != asyncWriteList.end(); it = remIt )
			{
				remIt = it;
				++remIt;
				if( (*it)->isCancelled() )
				{
					ipCallback->OnCancelComplete( (*it)->getTransactionID(), clientHandle );
				}
				else
				{
					if( (*it)->getCounts() != 0 )
						doAsyncWrite( &ipCallback, (*it) );
				}
				delete (*it);
				asyncWriteList.erase( it );
			}
			ipCallback->Release();
		}

		void Group::onUpdateTimer()
		{
			IOPCDataCallback* ipCallback = NULL;
			HRESULT hResult = getCallback( IID_IOPCDataCallback, (IUnknown**)&ipCallback );

			if( FAILED( hResult ) )
				return;

			std::list< OPCHANDLE > handles;
			lock::Mutex::ScopeGuard guard( groupGuard );
			for( std::map< OPCHANDLE, GroupItem*>::iterator it = itemList.begin(); it != itemList.end(); ++it )
			{
				if( actived && (*it).second->isActived() && (*it).second->isChange() )
				{
					handles.push_back( (*it).first );
				}
			}
			
			if( handles.size() != 0 )
			{
				AsyncRequest *request = new AsyncRequest( handles );
				request->setTransactionID( 0 );
				doAsyncRefresh( &ipCallback, request );
				delete request;
			}
			ipCallback->Release();
		}

		void Group::doAsyncRead( IOPCDataCallback** callBack, const AsyncRequest *request )
		{
			size_t counts = request->getCounts();

			OPCHANDLE *pHandles = NULL;
			VARIANT *pValue = NULL;
			WORD *pQuality = NULL;
			FILETIME *pTimeStamp = NULL;
			HRESULT *pErrors = NULL;

			pHandles = util::allocMemory< OPCHANDLE >( counts );
			if( pHandles == NULL )
				return;

			pValue = util::allocMemory< VARIANT >( counts );
			if( pValue == NULL )
			{
				util::freeMemory( pHandles );
				return;
			}

			pQuality = util::allocMemory< WORD >( counts );
			if( pQuality == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
			}

			pTimeStamp = util::allocMemory< FILETIME >( counts );
			if( pTimeStamp == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
				util::freeMemory( pQuality );
			}

			pErrors = util::allocMemory< HRESULT >( counts );
			if( pErrors == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
				util::freeMemory( pQuality );
				util::freeMemory( pTimeStamp );
			}

			HRESULT masterError = S_OK;
			const std::list< OPCHANDLE >  *handles = &request->getHandles();
			size_t i = 0;
			for( std::list< OPCHANDLE >::const_iterator it = handles->begin(); it != handles->end(); ++it, i++ )
			{
				std::map< OPCHANDLE, GroupItem* >::iterator iter = itemList.find( (*it) );
				if( iter == itemList.end() )
				{
					masterError = S_FALSE;
					pErrors[i] = OPC_E_INVALIDHANDLE;
					continue;
				}
				
				pHandles[i] = (*iter).second->getClientHandle();

				try
				{
					pErrors[i] = ((*iter).second->readValue()).copyTo( pValue[i] );
				}
				catch( frl::opc::address_space::NotExistTag &ex )
				{
					ex.~NotExistTag();
					pErrors[i] = OPC_E_INVALIDHANDLE;
				}

				if( FAILED( pErrors[i] ) )
				{
					masterError = S_FALSE;
					continue;
				}

				pQuality[i] = OPC_QUALITY_GOOD; // TODO: get quality from device ?
				pTimeStamp[i] = (*iter).second->getTimeStamp();
			}

			(*callBack)->OnReadComplete( request->getTransactionID(),
														clientHandle,
														S_OK,
														masterError,
														(DWORD)counts,
														pHandles,
														pValue,
														pQuality,
														pTimeStamp,
														pErrors );
		}

		void Group::doAsyncRefresh( IOPCDataCallback** callBack, const AsyncRequest *request )
		{
			size_t counts = request->getCounts();

			OPCHANDLE *pHandles = NULL;
			VARIANT *pValue = NULL;
			WORD *pQuality = NULL;
			FILETIME *pTimeStamp = NULL;
			HRESULT *pErrors = NULL;

			pHandles = util::allocMemory< OPCHANDLE >( counts );
			if( pHandles == NULL )
				return;

			pValue = util::allocMemory< VARIANT >( counts );
			if( pValue == NULL )
			{
				util::freeMemory( pHandles );
				return;
			}

			pQuality = util::allocMemory< WORD >( counts );
			if( pQuality == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
			}

			pTimeStamp = util::allocMemory< FILETIME >( counts );
			if( pTimeStamp == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
				util::freeMemory( pQuality );
			}

			pErrors = util::allocMemory< HRESULT >( counts );
			if( pErrors == NULL )
			{
				util::freeMemory( pHandles );
				util::freeMemory( pValue );
				util::freeMemory( pQuality );
				util::freeMemory( pTimeStamp );
			}

			const std::list< OPCHANDLE >  *handles = &request->getHandles();
			size_t i = 0;
			for( std::list< OPCHANDLE >::const_iterator it = handles->begin(); it != handles->end(); ++it, i++ )
			{
				std::map< OPCHANDLE, GroupItem* >::iterator iter = itemList.find( (*it) );
				if( iter == itemList.end() )
				{
					pErrors[i] = OPC_E_INVALIDHANDLE;
					continue;
				}
				pHandles[i] = (*iter).second->getClientHandle();
				try
				{
					if( request->getSource() == OPC_DS_CACHE )
						pErrors[i] = ((*iter).second->getCachedValue()).copyTo( pValue[i] );
					else
						pErrors[i] = ((*iter).second->readValue()).copyTo( pValue[i] );
				}
				catch( frl::opc::address_space::NotExistTag &ex )
				{
					ex.~NotExistTag();
					pErrors[i] = OPC_E_INVALIDHANDLE;
				}

				if( FAILED( pErrors[i] ) )
					continue;

				pQuality[i] = OPC_QUALITY_GOOD; // TODO: get quality from device ?
				pTimeStamp[i] = (*iter).second->getTimeStamp();
			}

			if( actived )
				(*callBack)->OnDataChange(	request->getTransactionID(),
															clientHandle,
															S_OK,
															S_OK,
															(DWORD)counts,
															pHandles,
															pValue,
															pQuality,
															pTimeStamp,
															pErrors );
		}

		void Group::doAsyncWrite( IOPCDataCallback** callBack, const AsyncRequest *request )
		{

			size_t counts = request->getCounts();

			OPCHANDLE *pHandles = NULL;
			HRESULT *pErrors = NULL;

			pHandles = util::allocMemory< OPCHANDLE >( counts );
			if( pHandles == NULL )
				return;

			pErrors = util::allocMemory< HRESULT >( counts );
			if( pErrors == NULL )
			{
				util::freeMemory( pHandles );
				return;
			}

			HRESULT masterError = S_OK;

			const std::list< OPCHANDLE >  *handles = &request->getHandles();
			size_t i = 0;
			for( std::list< OPCHANDLE >::const_iterator it = handles->begin(); it != handles->end(); ++it, i++ )
			{
				std::map< OPCHANDLE, GroupItem* >::iterator iter = itemList.find( (*it) );
				if( iter == itemList.end() )
				{
					masterError = S_FALSE;
					pErrors[i] = OPC_E_INVALIDHANDLE;
					continue;
				}
				pHandles[i] = (*iter).second->getClientHandle();

				if(  ! ( (*iter).second->getAccessRights() & OPC_WRITEABLE ) )
				{
					masterError = S_FALSE;
					pErrors[i] = OPC_E_BADRIGHTS;
					continue;
				}

				if( request->getValues()[i].vt == VT_EMPTY )
				{
					masterError = S_FALSE;
					pErrors[i] = OPC_E_BADTYPE;
					continue;
				}

				pErrors[i] = (*iter).second->writeValue( request->getValues()[i] );

				if( FAILED( pErrors[i] ) )
				{
					masterError = S_FALSE;
					continue;
				}
			}

			(*callBack)->OnWriteComplete(	request->getTransactionID(),
															clientHandle,
															masterError,
															( DWORD )counts,
															pHandles,
															pErrors );
		}

		Group* Group::cloneFrom( const Group &group )
		{
			Group *newGroup = new Group();
			newGroup->registerInterface(IID_IOPCDataCallback);
			newGroup->name = group.name;
			newGroup->actived = False;
			newGroup->clsid = group.clsid;
			newGroup->deadband = group.deadband;
			newGroup->enabled = group.enabled;
			newGroup->keepAlive = group.keepAlive;
			newGroup->lastUpdate = group.lastUpdate;
			newGroup->localeID = group.localeID;
			newGroup->server = group.server;
			newGroup->serverHandle = util::getUniqueServerHandle();
			newGroup->tickOffset = group.tickOffset;
			newGroup->timeBias = group.timeBias;
			newGroup->updateRate = group.updateRate;
			newGroup->clientHandle = group.clientHandle;
			newGroup->deleted = group.deleted;

			for( std::map<OPCHANDLE, GroupItem*>::const_iterator it = group.itemList.begin(); it != group.itemList.end(); ++it )
			{
				GroupItem *item = GroupItem::cloneFrom( *(*it).second );
				newGroup->itemList.insert( std::pair< OPCHANDLE, GroupItem*> ( item->getServerHandle(), item ) );
			}

			newGroup->timerRead.setTimer( 1 );
			newGroup->timerWrite.setTimer( 1 );
			newGroup->timerRefresh.setTimer( 1 );
			newGroup->timerUpdate.setTimer( newGroup->updateRate );
			newGroup->timerRead.start();
			newGroup->timerWrite.start();
			newGroup->timerRefresh.start();
			return newGroup;
		}
	} //namespace opc
} //namespace FatRat Library

#endif /* FRL_PLATFORM_WIN32 */