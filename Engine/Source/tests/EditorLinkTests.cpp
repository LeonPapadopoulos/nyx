// The editor link over real sockets on 127.0.0.1, with both ends in this one program.
#include "BinaryArchive.h"
#include "EditorLink.h"
#include "Log.h"
#include "NetConnection.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx::Engine;
	using namespace Nyx::Net;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}

	// Calls step until done() is true, for at most the time limit. Connecting to a port nobody
	// listens on takes about two seconds on Windows.
	bool PumpUntil(const std::function<void()>& step, const std::function<bool()>& done, int timeLimitMs = 10000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			step();
			if (done())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	std::vector<std::byte> MakeBytes(size_t size, uint32_t seed)
	{
		std::vector<std::byte> bytes(size);
		uint32_t value = seed;
		for (std::byte& byte : bytes)
		{
			value = value * 1664525u + 1013904223u;
			byte = static_cast<std::byte>(value >> 24);
		}
		return bytes;
	}

	// A message type the link doesn't know, like the ones later changes add
	struct TestMessage
	{
		static constexpr EEditorLinkMessage Type = static_cast<EEditorLinkMessage>(1000);

		uint32_t Value = 0;

		void Write(BinaryWriter& writer) const
		{
			writer.WriteUInt32(Value);
		}
	};

	struct ConnectedPair
	{
		std::unique_ptr<Connection> Client;
		std::unique_ptr<Connection> Server;
	};

	ConnectedPair Connect(Listener& listener)
	{
		ConnectedPair pair;
		pair.Client = Connection::ConnectToLocalPort(listener.GetPort());
		Require(PumpUntil(
					[&]
					{
						pair.Client->Poll();
						if (!pair.Server)
						{
							pair.Server = listener.Accept();
						}
					},
					[&] { return pair.Client->IsConnected() && pair.Server; }),
			"Connecting to the listener failed: " + pair.Client->GetCloseReason());
		return pair;
	}

	// A game's end connecting and an editor's end accepted from the listener, both updated
	struct LinkPair
	{
		std::unique_ptr<EditorLink> Game;
		std::unique_ptr<EditorLink> Editor;

		void Update(Listener& listener, const std::function<void(EditorLink&)>& onEditorCreated)
		{
			Game->Update();
			if (!Editor)
			{
				if (std::unique_ptr<Connection> accepted = listener.Accept())
				{
					Editor = std::make_unique<EditorLink>(std::move(accepted), "NyxEditor");
					onEditorCreated(*Editor);
				}
			}
			if (Editor)
			{
				Editor->Update();
			}
		}
	};

	LinkPair StartLinks(Listener& listener, const std::function<void(EditorLink&)>& onEditorCreated = [](EditorLink&) {})
	{
		LinkPair links;
		links.Game = std::make_unique<EditorLink>(Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame");
		Require(PumpUntil([&] { links.Update(listener, onEditorCreated); }, [&] { return links.Editor != nullptr; }),
			"The editor didn't accept the game");
		return links;
	}

	void TestStreamReaderPieces()
	{
		std::vector<std::byte> stream;
		AppendMessage(stream, 7, MakeBytes(300, 1));
		AppendMessage(stream, 8, {});
		AppendMessage(stream, 65535, MakeBytes(5, 2));

		// The header is little-endian: size 300 = 0x12C, then type 7
		Require(stream[0] == std::byte{ 0x2C } && stream[1] == std::byte{ 0x01 } && stream[2] == std::byte{ 0 } &&
				stream[3] == std::byte{ 0 } && stream[4] == std::byte{ 7 } && stream[5] == std::byte{ 0 },
			"The message header isn't size (u32) and type (u16), little-endian");

		// One byte at a time: a message only comes out once all of it has arrived
		MessageStreamReader reader;
		std::vector<Message> messages;
		for (const std::byte byte : stream)
		{
			reader.Append(&byte, 1);
			while (std::optional<Message> message = reader.TryTake())
			{
				messages.push_back(std::move(*message));
			}
		}

		Require(messages.size() == 3, "The stream reader didn't find three messages");
		Require(messages[0].Type == 7 && messages[0].Payload == MakeBytes(300, 1), "The first message changed");
		Require(messages[1].Type == 8 && messages[1].Payload.empty(), "The empty message changed");
		Require(messages[2].Type == 65535 && messages[2].Payload == MakeBytes(5, 2), "The third message changed");
		Require(reader.GetBufferedSize() == 0 && !reader.HasFailed(), "The stream reader kept bytes");
	}

	void TestStreamReaderRefusesHugeMessages()
	{
		// Text read as a header announces a payload of about 540 MB ("GET " as a little-endian u32).
		// The reader fails as soon as the header is complete, without waiting for the payload.
		const std::string notAMessage = "GET / HTTP/1.1\r\n\r\n";
		std::vector<std::byte> stream;
		AppendMessage(stream, 5, MakeBytes(20, 9));
		stream.insert(stream.end(), reinterpret_cast<const std::byte*>(notAMessage.data()),
			reinterpret_cast<const std::byte*>(notAMessage.data()) + notAMessage.size());

		MessageStreamReader reader;
		reader.Append(stream.data(), stream.size());
		Require(reader.HasFailed(), "The stream reader didn't notice the huge payload size");

		// The message before the bad header can still be taken, nothing after it
		const std::optional<Message> before = reader.TryTake();
		Require(before && before->Type == 5 && before->Payload == MakeBytes(20, 9), "The message before the bad header got lost");
		Require(!reader.TryTake(), "The stream reader read past the bad header");
	}

	void TestLargeMessagesAndOrder(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// Larger than the system's socket buffers, so sending and receiving take many steps
		const std::vector<std::byte> large = MakeBytes(8 * 1024 * 1024, 3);
		pair.Client->Send(1, MakeBytes(10, 4));
		pair.Client->Send(2, large);
		pair.Client->Send(3, {});

		std::vector<Message> received;
		Require(PumpUntil(
					[&]
					{
						pair.Client->Poll();
						pair.Server->Poll();
						while (std::optional<Message> message = pair.Server->Receive())
						{
							received.push_back(std::move(*message));
						}
					},
					[&] { return received.size() == 3; }),
			"Not all messages arrived");

		Require(received[0].Type == 1 && received[0].Payload == MakeBytes(10, 4), "The first message changed");
		Require(received[1].Type == 2 && received[1].Payload == large, "The 8 MB message changed");
		Require(received[2].Type == 3 && received[2].Payload.empty(), "The empty message changed");
		Require(pair.Client->GetUnsentSize() == 0, "Bytes are left to send");
	}

	void TestClosing(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// A message sent right before closing still arrives, followed by the close
		pair.Server->Send(42, MakeBytes(100, 5));
		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetUnsentSize() == 0; }), "Sending stalled");
		pair.Server->Close("test is done");
		Require(pair.Server->GetCloseCause() == ECloseCause::ThisSide && pair.Server->GetCloseReason() == "test is done",
			"Close() didn't record its reason");

		Require(PumpUntil([&] { pair.Client->Poll(); }, [&] { return pair.Client->GetState() == EConnectionState::Closed; }),
			"The client didn't notice the close");
		Require(pair.Client->GetCloseCause() == ECloseCause::OtherSide, "The close wasn't put down to the other side");

		const std::optional<Message> last = pair.Client->Receive();
		Require(last && last->Type == 42 && last->Payload == MakeBytes(100, 5), "The last message got lost in the close");

		// Sending on a closed connection is ignored
		pair.Client->Send(1, MakeBytes(10, 6));
		Require(pair.Client->GetUnsentSize() == 0, "A closed connection queued a message");
	}

	void TestClosingWithUnreadData(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// The client closes while a message for it has arrived but hasn't been read. Unread bytes
		// would make the system reset the connection; Close() reads them first, so the server sees
		// a normal close, and the message can still be taken.
		pair.Server->Send(43, MakeBytes(50, 10));
		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetUnsentSize() == 0; }), "Sending stalled");
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		pair.Client->Close("test is done");

		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetState() == EConnectionState::Closed; }),
			"The server didn't notice the close");
		Require(pair.Server->GetCloseCause() == ECloseCause::OtherSide,
			"Closing with unread data wasn't a normal close: " + pair.Server->GetCloseReason());

		const std::optional<Message> unread = pair.Client->Receive();
		Require(unread && unread->Type == 43 && unread->Payload == MakeBytes(50, 10), "The unread message got lost in the close");
	}

	void TestSendingTooLargeAMessage(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);
		pair.Client->Send(1, std::vector<std::byte>(MaxMessagePayloadSize + 1));
		Require(pair.Client->GetState() == EConnectionState::Closed && pair.Client->GetCloseCause() == ECloseCause::Error &&
				Contains(pair.Client->GetCloseReason(), "larger than the limit"),
			"Sending a message over the limit didn't fail clearly: " + pair.Client->GetCloseReason());
	}

	uint16_t GetPortNobodyListensOn()
	{
		Listener probe;
		Require(probe.Listen(0), "Listening failed: " + probe.GetError());
		const uint16_t port = probe.GetPort();
		probe.Close();
		return port;
	}

	void TestConnectingToNobody()
	{
		const uint16_t port = GetPortNobodyListensOn();
		std::unique_ptr<Connection> connection = Connection::ConnectToLocalPort(port);
		Require(PumpUntil([&] { connection->Poll(); }, [&] { return connection->GetState() == EConnectionState::Closed; }),
			"Connecting to a port nobody listens on didn't fail");
		Require(connection->GetCloseCause() == ECloseCause::Error, "The failed connect wasn't an error");
		Require(connection->GetCloseReason().find("couldn't connect to port " + std::to_string(port)) == 0,
			"Unexpected reason: " + connection->GetCloseReason());

		// The link reports it, and is Closed
		EditorLink link(Connection::ConnectToLocalPort(port), "NyxGame");
		Require(PumpUntil([&] { link.Update(); }, [&] { return link.GetState() == EEditorLinkState::Closed; }),
			"The link didn't notice that connecting failed");
		Require(link.GetCloseReason().find("couldn't connect") == 0, "Unexpected reason: " + link.GetCloseReason());
	}

	// Returns this process's id, as the Hellos report it
	uint32_t TestHandshake(Listener& listener)
	{
		LinkPair links;
		links.Game = std::make_unique<EditorLink>(Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame");

		// Sent before the connection is established: it still goes out after the Hello
		links.Game->Send(TestMessage{ 7 });
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&] { return links.Game->IsConnected() && links.Editor && links.Editor->IsConnected(); }),
			"The handshake didn't finish");

		std::optional<Message> early;
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); }, [&] { return (early = links.Editor->Receive()).has_value(); }),
			"A message sent before the handshake didn't arrive");
		BinaryReader earlyReader;
		earlyReader.LoadFromMemory(early->Payload);
		uint32_t earlyValue = 0;
		Require(early->Type == 1000 && earlyReader.ReadUInt32(earlyValue) && earlyValue == 7, "A message sent before the handshake changed");

		const HelloMessage& editor = links.Game->GetOtherSide();
		const HelloMessage& game = links.Editor->GetOtherSide();
		Require(editor.ProgramName == "NyxEditor" && game.ProgramName == "NyxGame", "The program names didn't arrive");
		Require(editor.ProtocolVersion == EditorLinkProtocolVersion && game.ProtocolVersion == EditorLinkProtocolVersion,
			"The protocol versions didn't arrive");
		Require(editor.ProcessId != 0 && editor.ProcessId == game.ProcessId, "The Hellos don't carry this process's id");

		// Messages after the handshake are handed out, in order
		links.Editor->Send(TestMessage{ 1 });
		links.Editor->Send(TestMessage{ 2 });
		std::vector<Message> received;
		Require(PumpUntil(
					[&]
					{
						links.Update(listener, [](EditorLink&) {});
						while (std::optional<Message> message = links.Game->Receive())
						{
							received.push_back(std::move(*message));
						}
					},
					[&] { return received.size() == 2; }),
			"Messages after the handshake didn't arrive");

		for (uint32_t i = 0; i < 2; ++i)
		{
			BinaryReader reader;
			reader.LoadFromMemory(received[i].Payload);
			uint32_t value = 0;
			Require(received[i].Type == 1000 && reader.ReadUInt32(value) && value == i + 1, "A message changed on the way");
		}

		// A second Hello is a protocol error: the game closes the link, and the editor sees that
		links.Editor->Send(HelloMessage{});
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&]
					{
						return links.Game->GetState() == EEditorLinkState::Closed &&
							links.Editor->GetState() == EEditorLinkState::Closed;
					}),
			"A second Hello didn't close the link");
		Require(Contains(links.Game->GetCloseReason(), "said Hello a second time"), "Unexpected reason: " + links.Game->GetCloseReason());

		return editor.ProcessId;
	}

	void TestRequiredProcessId(Listener& listener, uint32_t thisProcessId)
	{
		{
			LinkPair links = StartLinks(listener, [&](EditorLink& editor) { editor.RequireOtherProcessId(thisProcessId); });
			Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
						[&] { return links.Game->IsConnected() && links.Editor->IsConnected(); }),
				"The link didn't accept the right process id");
		}

		LinkPair links = StartLinks(listener, [&](EditorLink& editor) { editor.RequireOtherProcessId(thisProcessId + 1); });
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&]
					{
						return links.Game->GetState() == EEditorLinkState::Closed &&
							links.Editor->GetState() == EEditorLinkState::Closed;
					}),
			"The link accepted another process");
		Require(!links.Editor->IsConnected() && Contains(links.Editor->GetCloseReason(), "is meant for process"),
			"Unexpected reason: " + links.Editor->GetCloseReason());
	}

	// The other end is a plain connection, to send what an EditorLink never would
	void TestBadHellos(Listener& listener)
	{
		// A newer protocol version
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");

			HelloMessage hello;
			hello.ProtocolVersion = EditorLinkProtocolVersion + 1;
			hello.ProgramName = "NyxGame";
			BinaryWriter writer;
			hello.Write(writer);
			pair.Client->Send(static_cast<uint16_t>(EEditorLinkMessage::Hello), writer.GetBytes());

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A different protocol version didn't close the link");
			Require(Contains(editor.GetCloseReason(), "NyxGame speaks editor link protocol " + std::to_string(EditorLinkProtocolVersion + 1)),
				"Unexpected reason: " + editor.GetCloseReason());

			// The editor said its own Hello before closing, and the other side can read it
			Require(PumpUntil([&] { pair.Client->Poll(); }, [&] { return pair.Client->GetState() == EConnectionState::Closed; }),
				"The other side didn't notice the close");
			const std::optional<Message> editorHello = pair.Client->Receive();
			Require(editorHello && editorHello->Type == static_cast<uint16_t>(EEditorLinkMessage::Hello), "The editor didn't say Hello");
			BinaryReader reader;
			reader.LoadFromMemory(editorHello->Payload);
			HelloMessage read;
			Require(read.Read(reader) && read.ProgramName == "NyxEditor", "The editor's Hello can't be read");
		}

		// Something other than Hello first
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");
			pair.Client->Send(99, MakeBytes(4, 7));

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A message before Hello didn't close the link");
			Require(Contains(editor.GetCloseReason(), "message type 99 before saying Hello"), "Unexpected reason: " + editor.GetCloseReason());
		}

		// A Hello too short to read
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");
			pair.Client->Send(static_cast<uint16_t>(EEditorLinkMessage::Hello), MakeBytes(2, 8));

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A broken Hello didn't close the link");
			Require(Contains(editor.GetCloseReason(), "Hello can't be read"), "Unexpected reason: " + editor.GetCloseReason());
		}
	}

	void TestListenerWithoutWaitingPrograms(Listener& listener)
	{
		Require(listener.IsListening() && listener.GetPort() != 0, "The listener isn't listening");
		Require(listener.Accept() == nullptr, "Accept() returned a connection nobody made");
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();

		TestStreamReaderPieces();
		TestStreamReaderRefusesHugeMessages();

		Listener listener;
		Require(listener.Listen(0), "Listening failed: " + listener.GetError());

		TestListenerWithoutWaitingPrograms(listener);
		TestLargeMessagesAndOrder(listener);
		TestClosing(listener);
		TestClosingWithUnreadData(listener);
		TestSendingTooLargeAMessage(listener);
		TestConnectingToNobody();
		const uint32_t thisProcessId = TestHandshake(listener);
		TestRequiredProcessId(listener, thisProcessId);
		TestBadHellos(listener);

		std::cout << "All editor link tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor link test failed: " << error.what() << '\n';
		return 1;
	}
}
