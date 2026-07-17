import { useConversations } from './hooks/useConversations'
import Sidebar from './components/Sidebar'
import ChatWindow from './components/ChatWindow'

export default function App() {
  const {
    conversations,
    currentId,
    currentMessages,
    createConversation,
    switchConversation,
    deleteConversation,
    updateMessages,
  } = useConversations()

  return (
    <div className="flex h-screen bg-gray-950 text-white">
      <Sidebar
        conversations={conversations}
        currentId={currentId}
        onCreate={createConversation}
        onSwitch={switchConversation}
        onDelete={deleteConversation}
      />
      <div className="flex-1 min-w-0">
        <ChatWindow
          key={currentId}
          messages={currentMessages}
          onUpdateMessages={(msgs) => updateMessages(currentId, msgs)}
        />
      </div>
    </div>
  )
}
