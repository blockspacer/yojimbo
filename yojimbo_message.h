/*
    Yojimbo Network Library.
    
    Copyright © 2016 - 2017, The Network Protocol Company, Inc.
*/

#ifndef YOJIMBO_MESSAGE_H
#define YOJIMBO_MESSAGE_H

#include "yojimbo_config.h"
#include "yojimbo_stream.h"
#include "yojimbo_serialize.h"
#include "yojimbo_allocator.h"

#if YOJIMBO_DEBUG_MESSAGE_LEAKS
#include <map>
#endif // #if YOJIMBO_DEBUG_MESSAGE_LEAKS

/** @file */

namespace yojimbo
{
    /**
        A reference counted object that can be serialized to a bitstream.

        Messages are objects that are sent between client and server across the connection. They are carried inside the ConnectionPacket generated by the Connection class. Messages can be sent reliable-ordered, or unreliable-unordered, depending on the configuration of the channel they are sent over.

        To use messages, create your own set of message classes by inheriting from this class (or from BlockMessage, if you want to attach data blocks to your message), then setup an enum of all your message types and derive a message factory class to create your message instances by type.

        There are macros to help make defining your message factory painless:

            YOJIMBO_MESSAGE_FACTORY_START
            YOJIMBO_DECLARE_MESSAGE_TYPE
            YOJIMBO_MESSAGE_FACTORY_FINISH

        Once you have a message factory, register it with your declared inside your client and server classes using:

            YOJIMBO_MESSAGE_FACTORY

        which overrides the Client::CreateMessageFactory and Server::CreateMessageFactory methods so the client and server classes use your message factory type.

        See tests/shared.h for an example showing you how to do this, and the functional tests inside tests/test.cpp for examples showing how how to send and receive messages.
        
        @see BlockMessage
        @see MessageFactory
        @see Connection
     */

    class Message : public Serializable
    {
    public:

        /**
            Message constructor.

            Don't call this directly, use a message factory instead.

            @param blockMessage 1 if this is a block message, 0 otherwise.

            @see MessageFactory::Create
         */

        Message( int blockMessage = 0 ) : m_refCount(1), m_id(0), m_type(0), m_blockMessage( blockMessage ) {}

        /** 
            Set the message id.

            When messages are sent over a reliable-ordered channel, the message id starts at 0 and increases with each message sent over that channel.

            When messages are sent over an unreliable-unordered channel, the message id is set to the sequence number of the packet it was delivered in.

            @param id The message id.
         */

        void SetId( uint16_t id ) { m_id = id; }

        /**
            Get the message id.

            @returns The message id.
         */

        int GetId() const { return m_id; }

        /**
            Get the message type.

            This corresponds to the type enum value used to create the message in the message factory.

            @returns The message type.

            @see MessageFactory.
         */

        int GetType() const { return m_type; }

        /**
            Get the reference count on the message.

            Messages start with a reference count of 1 when they are created. This is decreased when they are released. 

            When the reference count reaches 0, the message is destroyed.

            @returns The reference count on the message.
         */

        int GetRefCount() { return m_refCount; }

        /**
            Is this a block message?

            Block messages are of type BlockMessage and can have a data block attached to the message.

            @returns True if this is a block message, false otherwise.

            @see BlockMessage.
         */

        bool IsBlockMessage() const { return m_blockMessage; }

        /**
            Virtual serialize function (read).

            Reads the message in from a bitstream.

            Don't override this method directly, instead, use the YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS macro in your derived message class to redirect it to a templated serialize method.

            This way you can implement serialization for your packets in a single method and the C++ compiler takes care of generating specialized read, write and measure implementations for you. 

            See tests/shared.h for some examples of this.
         */

        virtual bool SerializeInternal( ReadStream & stream ) = 0;

        /**
            Virtual serialize function (write).

            Write the message to a bitstream.

            Don't override this method directly, instead, use the YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS macro in your derived message class to redirect it to a templated serialize method.

            This way you can implement serialization for your packets in a single method and the C++ compiler takes care of generating specialized read, write and measure implementations for you. 

            See tests/shared.h for some examples of this.
         */

        virtual bool SerializeInternal( WriteStream & stream ) = 0;

        /**
            Virtual serialize function (measure).

            Measure how many bits this message would take to write. This is used when working out how many messages will fit within the channel packet budget.

            Don't override this method directly, instead, use the YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS macro in your derived message class to redirect it to a templated serialize method.

            This way you can implement serialization for your packets in a single method and the C++ compiler takes care of generating specialized read, write and measure implementations for you. 

            See tests/shared.h for some examples of this.
         */

        virtual bool SerializeInternal ( MeasureStream & stream ) = 0;

    protected:

        /**
            Set the message type.

            Called by the message factory after it creates a message.

            @param type The message type.
         */

        void SetType( int type ) { m_type = type; }

        /**
            Add a reference to the message.

            This is called when a message is included in a packet and added to the receive queue. 

            This way we don't have to pass messages by value (more efficient) and messages get cleaned up when they are delivered and no packets refer to them.
         */

        void Acquire() { assert( m_refCount > 0 ); m_refCount++; }

        /**
            Remove a reference from the message.

            Message are deleted when the number of references reach zero. Messages have reference count of 1 after creation.
         */

        void Release() { assert( m_refCount > 0 ); m_refCount--; }

        /**
            Message destructor.

            Protected because you aren't supposed delete messages directly because they are reference counted. Use MessageFactory::Release instead.

            @see MessageFactory::Release
         */

        virtual ~Message()
        {
            assert( m_refCount == 0 );
        }

    private:

        friend class MessageFactory;
      
        Message( const Message & other );
        
        const Message & operator = ( const Message & other );

        int m_refCount;                                                     ///< Number of references on this message object. Starts at 1. Message is destroyed when it reaches 0.
        uint32_t m_id : 16;                                                 ///< The message id. For messages sent over reliable-ordered channels, this starts at 0 and increases with each message sent. For unreliable-unordered channels this is set to the sequence number of the packet the message was included in.
        uint32_t m_type : 15;                                               ///< The message type. Corresponds to the type integer used when the message was created though the message factory.
        uint32_t m_blockMessage : 1;                                        ///< 1 if this is a block message. 0 otherwise. If 1 then you can cast the Message* to BlockMessage*. In short, it's a lightweight RTTI.
    };

    /**
        A message that can have a block of data attached to it.

        Attaching blocks of data is very useful, especially over a reliable-ordered channel where these blocks can be larger that the maximum packet size. Blocks sent over a reliable-ordered channel are automatically split up into fragments and reassembled on the other side.

        This gives you have the convenience of a reliable-ordered control messages, while attaching large blocks of data (larger than max packet size), while having all messages delivered reliably and in-order. 

        Situations where this can be useful is when sending down the initial state of the world on client connect, or block of configuration data to send up from the client to server on connect.

        It can also be used for messages sent across an unreliable-unordered channel, but in that case blocks aren't split up into fragments. Make sure you consider this when designing your channel budgets when sending blocks over unreliable-unordered channels.

        @see ChannelConfig
     */

    class BlockMessage : public Message
    {
    public:

        /**
            Block message constructor.

            Don't call this directly, use a message factory instead.

            @see MessageFactory::CreateMessage
         */

        explicit BlockMessage() : Message( 1 ), m_allocator(NULL), m_blockData(NULL), m_blockSize(0) {}

        /**
            Attach a block to this message.

            You can only attach one block. This method will assert if a block is already attached.
         */

        void AttachBlock( Allocator & allocator, uint8_t * blockData, int blockSize )
        {
            assert( blockData );
            assert( blockSize > 0 );
            assert( !m_blockData );

            m_allocator = &allocator;
            m_blockData = blockData;
            m_blockSize = blockSize;
        }

        /** 
            Detach the block from this message.

            By doing this you are responsible for copying the block pointer and allocator and making sure the block is freed.

            This could be used for example, if you wanted to copy off the block and store it somewhere, without the cost of copying it.
         */

        void DetachBlock()
        {
            m_allocator = NULL;
            m_blockData = NULL;
            m_blockSize = 0;
        }

        /**
            Get the allocator used to allocate the block.

            @returns The allocator for the block. NULL if no block is attached to this message.
         */

        Allocator * GetAllocator()
        {
            return m_allocator;
        }

        /**
            Get the block data pointer.

            @returns The block data pointer. NULL if no block is attached.
         */

        uint8_t * GetBlockData()
        {
            return m_blockData;
        }

        /**
            Get a constant pointer to the block data.

            @returns A constant pointer to the block data. NULL if no block is attached.
         */

        const uint8_t * GetBlockData() const
        {
            return m_blockData;
        }

        /**
            Get the size of the block attached to this message.

            @returns The size of the block (bytes). 0 if no block is attached.
         */

        int GetBlockSize() const
        {
            return m_blockSize;
        }

        /**
            Templated serialize function for the block message. Doesn't do anything. The block data is serialized elsewhere.

            You can override the serialize methods on a block message to implement your own serialize function. It's just like a regular message with a block attached to it.

            @see ConnectionPacket
            @see ChannelPacketData
            @see ReliableOrderedChannel
            @see UnreliableUnorderedChannel
         */

        template <typename Stream> bool Serialize( Stream & stream ) { (void) stream; return true; }

        YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();

    protected:

        /**
            If a block was attached to the message, it is freed here.
         */

        ~BlockMessage()
        {
            if ( m_allocator )
            {
                YOJIMBO_FREE( *m_allocator, m_blockData );
                m_blockSize = 0;
                m_allocator = NULL;
            }
        }

    private:

        Allocator * m_allocator;                                                ///< Allocator for the block attached to the message. NULL if no block is attached.
        uint8_t * m_blockData;                                                  ///< The block data. NULL if no block is attached.
        int m_blockSize;                                                        ///< The block size (bytes). 0 if no block is attached.
    };

    /**
        Message factory error level.
     */

    enum MessageFactoryErrorLevel
    {
        MESSAGE_FACTORY_ERROR_NONE,                                             ///< No error. All is well.
        MESSAGE_FACTORY_ERROR_FAILED_TO_ALLOCATE_MESSAGE,                       ///< Failed to allocate a message. Typically this means we ran out of memory on the allocator backing the message factory.
    };

    /**
        Defines the set of message types that can be created.

        You can derive a message factory yourself to create your own message types, or you can use these helper macros to do it for you:

            YOJIMBO_MESSAGE_FACTORY_START
            YOJIMBO_DECLARE_MESSAGE_TYPE
            YOJIMBO_MESSAGE_FACTORY_FINISH

        See tests/shared.h for an example showing how to use the macros.
     */

    class MessageFactory
    {        
        #if YOJIMBO_DEBUG_MESSAGE_LEAKS
        std::map<void*,int> allocated_messages;                                 ///< The set of allocated messages for this factory. Used to track down message leaks.
        #endif // #if YOJIMBO_DEBUG_MESSAGE_LEAKS

        Allocator * m_allocator;                                                ///< The allocator used to create messages.

        int m_numTypes;                                                         ///< The number of message types.
        
        MessageFactoryErrorLevel m_errorLevel;                                  ///< The message factory error level.

    public:

        /**
            Message factory allocator.

            Pass in the number of message types for the message factory from the derived class.

            @param allocator The allocator used to create messages.
            @param numTypes The number of message types. Valid types are in [0,numTypes-1].
         */

        MessageFactory( Allocator & allocator, int numTypes )
        {
            m_allocator = &allocator;
            m_numTypes = numTypes;
            m_errorLevel = MESSAGE_FACTORY_ERROR_NONE;
        }

        /**
            Message factory destructor.

            Checks for message leaks if YOJIMBO_DEBUG_MESSAGE_LEAKS is defined and not equal to zero. This is on by default in debug build.
         */

        virtual ~MessageFactory()
        {
            assert( m_allocator );

            m_allocator = NULL;

            #if YOJIMBO_DEBUG_MESSAGE_LEAKS
            if ( allocated_messages.size() )
            {
                printf( "you leaked messages!\n" );
                printf( "%d messages leaked\n", (int) allocated_messages.size() );
                typedef std::map<void*,int>::iterator itor_type;
                for ( itor_type i = allocated_messages.begin(); i != allocated_messages.end(); ++i ) 
                {
                    Message * message = (Message*) i->first;
                    printf( "leaked message %p (type %d, refcount %d)\n", message, message->GetType(), message->GetRefCount() );
                }
                exit(1);
            }
            #endif // #if YOJIMBO_DEBUG_MESSAGE_LEAKS
        }

        /**
            Create a message by type.

            IMPORTANT: Check the message pointer returned by this call. It can be NULL if there is no memory to create a message!

            Messages returned from this function have one reference added to them. When you are finished with the message, pass it to MessageFactory::Release.

            @param type The message type in [0,numTypes-1].

            @returns The allocated message, or NULL if the message could not be allocated. If the message allocation fails, the message factory error level is set to MESSAGE_FACTORY_ERROR_FAILED_TO_ALLOCATE_MESSAGE.

            @see MessageFactory::AddRef
            @see MessageFactory::ReleaseMessage
         */

        Message * CreateMessage( int type )
        {
            assert( type >= 0 );
            assert( type < m_numTypes );

            Message * message = CreateMessageInternal( type );
            if ( !message )
            {
                m_errorLevel = MESSAGE_FACTORY_ERROR_FAILED_TO_ALLOCATE_MESSAGE;
                return NULL;
            }

            #if YOJIMBO_DEBUG_MESSAGE_LEAKS
            allocated_messages[message] = 1;
            assert( allocated_messages.find( message ) != allocated_messages.end() );
            #endif // #if YOJIMBO_DEBUG_MESSAGE_LEAKS

            return message;
        }

        /**
            Add a reference to a message.

            @param message The message to add a reference to.

            @see MessageFactory::Create
            @see MessageFactory::Release
         */   

        void AcquireMessage( Message * message )
        {
            assert( message );
            if ( message )
                message->Acquire();
        }

        /**
            Remove a reference from a message.

            Messages have 1 reference when created. When the reference count reaches 0, they are destroyed.

            @see MessageFactory::Create
            @see MessageFactory::AddRef
         */

        void ReleaseMessage( Message * message )
        {
            assert( message );
            if ( !message )
                return;

            message->Release();
            
            if ( message->GetRefCount() == 0 )
            {
                #if YOJIMBO_DEBUG_MESSAGE_LEAKS
                assert( allocated_messages.find( message ) != allocated_messages.end() );
                allocated_messages.erase( message );
                #endif // #if YOJIMBO_DEBUG_MESSAGE_LEAKS
            
                assert( m_allocator );

                YOJIMBO_DELETE( *m_allocator, Message, message );
            }
        }

        /**
            Get the number of message types supported by this message factory.

            @returns The number of message types.
         */

        int GetNumTypes() const
        {
            return m_numTypes;
        }

        /**
            Get the allocator used to create messages.

            @returns The allocator.
         */

        Allocator & GetAllocator()
        {
            assert( m_allocator );
            return *m_allocator;
        }

        /**
            Get the error level.

            When used with a client or server, an error level on a message factory other than MESSAGE_FACTORY_ERROR_NONE triggers a client disconnect.
         */

        MessageFactoryErrorLevel GetErrorLevel() const
        {
            return m_errorLevel;
        }

        /**
            Clear the error level back to no error.
         */

        void ClearErrorLevel()
        {
            m_errorLevel = MESSAGE_FACTORY_ERROR_NONE;
        }

    protected:

        /**
            This method is overridden to create messages by type.

            @param type The type of message to be created.

            @returns The message created. Its reference count is 1.
         */

        virtual Message * CreateMessageInternal( int type ) { (void) type; return NULL; }

        /**
            Set the message type of a message.

            Put here because Message::SetMessageType is protected, but we need to be able to call this inside the overridden MessageFactory::CreateMessage method.
            
            @param message The message object.
            @param type The message type to set.
         */

        void SetMessageType( Message * message, int type ) { message->SetType( type ); }
    };
}

/** 
    Start a definition of a new message factory.

    This is a helper macro to make declaring your own message factory class easier.

    @param factory_class The name of the message factory class to generate.
    @param num_message_types The number of message types for this factory.

    See tests/shared.h for an example of usage.
 */

#define YOJIMBO_MESSAGE_FACTORY_START( factory_class, num_message_types )                                                               \
                                                                                                                                        \
    class factory_class : public MessageFactory                                                                                         \
    {                                                                                                                                   \
    public:                                                                                                                             \
        factory_class( yojimbo::Allocator & allocator ) : MessageFactory( allocator, num_message_types ) {}                             \
        yojimbo::Message * CreateMessageInternal( int type )                                                                            \
        {                                                                                                                               \
            Message * message;                                                                                                          \
            yojimbo::Allocator & allocator = GetAllocator();                                                                            \
            (void) allocator;                                                                                                           \
            switch ( type )                                                                                                             \
            {                                                                                                                           \

/** 
    Add a message type to a message factory.

    This is a helper macro to make declaring your own message factory class easier.

    @param message_type The message type value. This is typically an enum value.
    @param message_class The message class to instantiate when a message of this type is created.

    See tests/shared.h for an example of usage.
 */

#define YOJIMBO_DECLARE_MESSAGE_TYPE( message_type, message_class )                                                                     \
                                                                                                                                        \
                case message_type:                                                                                                      \
                    message = YOJIMBO_NEW( allocator, message_class );                                                                  \
                    if ( !message )                                                                                                     \
                        return NULL;                                                                                                    \
                    SetMessageType( message, message_type );                                                                            \
                    return message;

/** 
    Finish the definition of a new message factory.

    This is a helper macro to make declaring your own message factory class easier.

    See tests/shared.h for an example of usage.
 */

#define YOJIMBO_MESSAGE_FACTORY_FINISH()                                                                                                \
                                                                                                                                        \
                default: return NULL;                                                                                                   \
            }                                                                                                                           \
        }                                                                                                                               \
    };

#endif
